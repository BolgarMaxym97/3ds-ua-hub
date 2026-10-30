/*
 * The screenshots of the page open now (I1-I3): downloaded on a thread of their own when the page
 * opens, into the image cache the banners use. Only that page's: a whole catalog of them would be
 * megabytes nobody looks at.
 */
#ifndef SHOTS_H
#define SHOTS_H

#include "catalog.h"

typedef enum { SHOT_LOADING, SHOT_READY, SHOT_FAILED } ShotState;

/* Starts fetching `it`'s screenshots that are not cached yet; the last call wins. Failed ones are
 * tried again. */
void shots_want(const CatItem *it);
/* How the screenshot with this sha256 stands; SHOT_LOADING for one not asked for. */
ShotState shots_state(const char *sha256);
/* Leaving the app: waits for the download under way, skips the rest. */
void shots_exit(void);

#endif
