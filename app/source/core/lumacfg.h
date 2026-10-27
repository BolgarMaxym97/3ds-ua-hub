/*
 * Turning Luma's two flags on or off in luma/config.ini (from 3ds-ua's installer; the Hub needs only enable_game_patching).
 *
 * Luma resets its whole configuration to the defaults when it cannot parse the file, so
 * this edits only a file it fully understands: a config format it was checked against,
 * both keys present once under [boot] with a one-character 0/1 value. It changes exactly
 * those characters and leaves every other byte as it was.
 */
#ifndef CORE_LUMACFG_H
#define CORE_LUMACFG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *game;      /* the value character of enable_game_patching */
    char *external;  /* ... of enable_external_firm_and_modules */
} LumaConfig;

/* 0 when the file is one this can edit; `text` is edited in place through the pointers. */
int lumacfg_parse(LumaConfig *c, char *text, size_t len);

#endif
