/*
 * 240mp-launcherd — starts/stops the 240-MP app when the 240MP core loads.
 *
 * Runs permanently in the background on the MiSTer (installed via
 * /media/fat/linux/user-startup.sh).  Watches /tmp/CORENAME (written by
 * Main_MiSTer on every core change):
 *
 *   CORENAME == "240MP"  →  1. enable the Linux framebuffer scanout
 *                           2. exec the app (/media/fat/240mp/run.sh)
 *   CORENAME != "240MP"  →  SIGTERM the app if running
 *   app exits cleanly    →  return to the MiSTer menu (load menu.rbf)
 *
 * Framebuffer scanout: Main only displays the Linux console framebuffer
 * (buffer 0) when the "fb terminal" is active.  There is no /dev/MiSTer_cmd
 * command for it, but Main's global key handler toggles it on
 * Ctrl+Alt+F9 for any core (menu.cpp KEY_F9 handler, requires
 * cfg.fb_terminal=1 which is the default).  We synthesize that chord with a
 * short-lived uinput virtual keyboard — no Main_MiSTer modifications needed.
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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/uinput.h>

#define CORENAME_FILE "/tmp/CORENAME"
#define CORE_NAME     "240MP"
#define APP_CMD       "/media/fat/240mp/run.sh"
#define MISTER_CMD    "/dev/MiSTer_cmd"
#define MENU_RBF      "/media/fat/menu.rbf"
#define FB_SETUP_CMD  "fb_cmd1 8888 1 640 480"

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

/* ---- uinput: synthesize Ctrl+Alt+F9 so Main enables the fb terminal ---- */

static int emit(int fd, int type, int code, int value)
{
    struct input_event ev = {0};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return write(fd, &ev, sizeof ev) == sizeof ev ? 0 : -1;
}

static int inject_fb_terminal_chord(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "launcherd: open /dev/uinput: %s\n", strerror(errno));
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTCTRL);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTALT);
    ioctl(fd, UI_SET_KEYBIT, KEY_F9);

    struct uinput_user_dev ud = {0};
    snprintf(ud.name, sizeof ud.name, "240mp-launcher-kbd");
    ud.id.bustype = BUS_VIRTUAL;
    ud.id.vendor = 0x0240; ud.id.product = 0x00F9; ud.id.version = 1;
    if (write(fd, &ud, sizeof ud) != sizeof ud || ioctl(fd, UI_DEV_CREATE)) {
        fprintf(stderr, "launcherd: uinput create failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Main hotplugs input devices; give it a moment to pick ours up. */
    msleep(1200);

    emit(fd, EV_KEY, KEY_LEFTCTRL, 1); emit(fd, EV_SYN, SYN_REPORT, 0);
    emit(fd, EV_KEY, KEY_LEFTALT,  1); emit(fd, EV_SYN, SYN_REPORT, 0);
    msleep(50);
    emit(fd, EV_KEY, KEY_F9, 1);       emit(fd, EV_SYN, SYN_REPORT, 0);
    msleep(50);
    emit(fd, EV_KEY, KEY_F9, 0);       emit(fd, EV_SYN, SYN_REPORT, 0);
    emit(fd, EV_KEY, KEY_LEFTALT,  0); emit(fd, EV_SYN, SYN_REPORT, 0);
    emit(fd, EV_KEY, KEY_LEFTCTRL, 0); emit(fd, EV_SYN, SYN_REPORT, 0);

    msleep(200);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
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
            fprintf(stderr, "launcherd: %s core loaded\n", CORE_NAME);
            msleep(800);                       /* let the core finish init */
            inject_fb_terminal_chord();        /* console fb scanout on */
            msleep(500);
            mister_cmd(FB_SETUP_CMD);          /* accepted now fb is active */
            msleep(200);
            app = start_app();
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
