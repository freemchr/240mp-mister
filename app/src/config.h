/*
 * config.h — minimal persistent settings at /media/fat/240mp/config.json.
 * Shape mirrors the parent project: {"app":{...},"modules":{...}}. For now
 * only a few app-level keys are read/written; a fuller parser lands with the
 * server backends in later phases. GPL-3.0.
 */
#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_PATH "/media/fat/240mp/config.json"

void config_load(void);
void config_save(void);

/* app-level string setting (returns fallback if unset) */
const char *config_get(const char *key, const char *fallback);
void        config_set(const char *key, const char *value);

#endif /* CONFIG_H */
