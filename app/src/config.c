/* config.c — tiny config store. See config.h. GPL-3.0.
 *
 * Deliberately minimal: an in-memory table of app-level key/value strings,
 * serialized as a small JSON document compatible with the parent project's
 * layout. The reader is a forgiving string scan (not a full JSON parser) —
 * sufficient for the handful of scalar app settings used so far.
 */
#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_KV 32
static struct { char *k, *v; } g_kv[MAX_KV];
static int g_n;

static void set_internal(const char *k, const char *v)
{
    for (int i = 0; i < g_n; i++)
        if (!strcmp(g_kv[i].k, k)) {
            free(g_kv[i].v); g_kv[i].v = strdup(v); return;
        }
    if (g_n < MAX_KV) {
        g_kv[g_n].k = strdup(k);
        g_kv[g_n].v = strdup(v);
        g_n++;
    }
}

const char *config_get(const char *key, const char *fallback)
{
    for (int i = 0; i < g_n; i++)
        if (!strcmp(g_kv[i].k, key)) return g_kv[i].v;
    return fallback;
}

void config_set(const char *key, const char *value) { set_internal(key, value); }

/* Scan for "app":{ ... } and pull out "key":"value" string pairs. */
void config_load(void)
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(f); return; }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, sz, f);
    buf[got] = 0;
    fclose(f);

    const char *app = strstr(buf, "\"app\"");
    const char *scan = app ? app : buf;
    /* naive: find each "key":"value" pair */
    const char *p = scan;
    while ((p = strchr(p, '"'))) {
        char key[128], val[512];
        const char *ks = p + 1;
        const char *ke = strchr(ks, '"');
        if (!ke) break;
        size_t kl = ke - ks;
        const char *colon = ke + 1;
        while (*colon == ' ') colon++;
        if (*colon != ':') { p = ke + 1; continue; }
        const char *vs = colon + 1;
        while (*vs == ' ') vs++;
        if (*vs != '"') { p = ke + 1; continue; }  /* only string values */
        vs++;
        const char *ve = strchr(vs, '"');
        if (!ve) break;
        size_t vl = ve - vs;
        if (kl < sizeof key && vl < sizeof val && strncmp(ks, "app", kl)
            && strncmp(ks, "modules", kl)) {
            memcpy(key, ks, kl); key[kl] = 0;
            memcpy(val, vs, vl); val[vl] = 0;
            set_internal(key, val);
        }
        p = ve + 1;
    }
    free(buf);
}

void config_save(void)
{
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "{\n  \"app\": {\n");
    for (int i = 0; i < g_n; i++)
        fprintf(f, "    \"%s\": \"%s\"%s\n", g_kv[i].k, g_kv[i].v,
                i + 1 < g_n ? "," : "");
    fprintf(f, "  },\n  \"modules\": {}\n}\n");
    fclose(f);
}
