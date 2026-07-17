/*
 * 240mp-launcherd — starts/stops the 240-MP app when the 240MP core loads.
 *
 * Runs permanently in the background on the MiSTer (installed via
 * /media/fat/linux/user-startup.sh).  Watches /tmp/CORENAME (written by
 * Main_MiSTer on every core change):
 *
 *   CORENAME == "240MP"  →  1. clear the core's fabric framebuffer (it scans
 *                              out DDR at 0x20000000, garbage after power-up)
 *                           2. exec the app (/media/fat/240mp/run.sh)
 *   CORENAME != "240MP"  →  SIGTERM the app if running
 *   app exits cleanly    →  return to the MiSTer menu (load menu.rbf)
 *
 * The 240MP core owns its framebuffer in fabric (MISTER_FB), so no
 * Main_MiSTer framebuffer-terminal state is involved.  (Historical note:
 * v1 tried Main's Ctrl+Alt+F9 fb-terminal chord via uinput — it works on
 * the menu core but never triggered on a custom core, hence the fabric fb.)
 *
 * Static-linked, no dependencies. GPL-3.0.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define CORENAME_FILE "/tmp/CORENAME"
#define CORE_NAME     "240MP"
#define APP_CMD       "/media/fat/240mp/run.sh"
#define MISTER_CMD    "/dev/MiSTer_cmd"
#define MENU_RBF      "/media/fat/menu.rbf"
#define FB_BASE       0x21000000u        /* fabric framebuffer (MP240.sv FB_BASE) */
#define FB_BYTES      (640u * 480u * 4u)

static volatile sig_atomic_t g_running = 1;
static void on_term(int sig) { (void)sig; g_running = 0; }

static void msleep(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void read_corename(char *buf, size_t len)
{
    buf[0] = 0;
    FILE *f = fopen(CORENAME_FILE, "r");
    if (!f) return;
    size_t n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = 0;
    buf[strcspn(buf, "\r\n")] = 0;
}

static void mister_cmd(const char *cmd)
{
    int fd = open(MISTER_CMD, O_WRONLY);
    if (fd < 0) return;
    dprintf(fd, "%s", cmd);
    close(fd);
}

static void logts(const char *fmt, const char *arg)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stderr, "[%5lld.%03ld] launcherd: ", (long long)ts.tv_sec, ts.tv_nsec / 1000000);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
}


/* ---- clear the fabric framebuffer (uninitialized DDR = garbage) ------- */

static void clear_framebuffer(void)
{
    int dm = open("/dev/mem", O_RDWR | O_SYNC);
    if (dm < 0) {
        logts("open /dev/mem failed: %s", strerror(errno));
        return;
    }
    void *p = mmap(NULL, FB_BYTES, PROT_WRITE, MAP_SHARED, dm, FB_BASE);
    close(dm);
    if (p == MAP_FAILED) {
        logts("mmap fb region failed: %s", strerror(errno));
        return;
    }
    memset(p, 0, FB_BYTES);
    munmap(p, FB_BYTES);
    logts("framebuffer cleared%s", "");
}

/* ------------------------------- app control --------------------------- */

static pid_t start_app(void)
{
    if (access(APP_CMD, X_OK) != 0) {
        fprintf(stderr, "launcherd: %s missing or not executable\n", APP_CMD);
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", APP_CMD, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void stop_app(pid_t pid)
{
    if (pid <= 0) return;
    kill(-pid, SIGTERM);           /* whole process group (run.sh + app) */
    for (int i = 0; i < 20; i++) { /* up to 2 s graceful */
        if (waitpid(pid, NULL, WNOHANG) == pid) return;
        msleep(100);
    }
    kill(-pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

int main(void)
{
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    char core[64], prev[64] = "";
    pid_t app = -1;

    fprintf(stderr, "launcherd: watching %s for %s\n", CORENAME_FILE, CORE_NAME);

    while (g_running) {
        read_corename(core, sizeof core);
        int is_ours = strcmp(core, CORE_NAME) == 0;
        int was_ours = strcmp(prev, CORE_NAME) == 0;

        if (is_ours && !was_ours) {
            logts("%s core loaded", CORE_NAME);
            clear_framebuffer();               /* ASAP: garbage → black */
            app = start_app();
            logts("app started%s", "");
        }
        else if (!is_ours && was_ours) {
            fprintf(stderr, "launcherd: core changed to '%s', stopping app\n", core);
            stop_app(app);
            app = -1;
        }

        /* app exited on its own while our core is up → back to the menu */
        if (app > 0) {
            int st;
            pid_t r = waitpid(app, &st, WNOHANG);
            if (r == app) {
                fprintf(stderr, "launcherd: app exited (status %d)\n",
                        WIFEXITED(st) ? WEXITSTATUS(st) : -1);
                app = -1;
                if (is_ours) mister_cmd("load_core " MENU_RBF);
            }
        }

        strcpy(prev, core);
        msleep(500);
    }

    stop_app(app);
    return 0;
}
