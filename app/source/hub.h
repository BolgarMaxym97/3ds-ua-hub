/*
 * Getting the catalog on a thread of its own, so the screens keep drawing: from the Worker
 * (signed), else from the copy on the card (B3), then the banners and icons it names.
 */
#ifndef HUB_H
#define HUB_H

#include <3ds.h>
#include <stdbool.h>

#include "catalog.h"
#include "net.h"
#include "titles.h"

typedef enum {
    LOAD_RUNNING,
    LOAD_ONLINE,     /* fresh from the Worker */
    LOAD_OFFLINE,    /* no network: the copy from the last time (B3) */
    LOAD_NONE,       /* no network and no copy yet (A3) */
    LOAD_BROKEN,     /* the server answers, but nothing it sends checks out (SERVER) */
    LOAD_TOO_NEW,    /* the catalog needs a newer app (A4) */
} LoadState;

typedef struct {
    volatile LoadState state;   /* written by the thread, read by the screens */
    NetResult net;              /* why the network part failed, when it did */
    Catalog catalog;            /* the screens' copy: swapped in by hub_poll() only */
    char fetched_at[32];        /* "25.10, 14:02" for B3 */
    char need_app[16];          /* LOAD_TOO_NEW: the app version the new catalog asks for, if it says */
    volatile int images_left;   /* banners and icons still downloading */
    Titles titles;              /* the games on the console: swapped in by hub_poll() */
    volatile bool titles_ready;
    volatile bool scanning;     /* the thread is reading the games on the console */
    int titles_gen;             /* counts the swaps of `titles`: the screens re-check on a change */
    volatile bool early;        /* RUNNING, and the copy from the card is out: the catalog can open */
    bool loaded_once;           /* the thread's: the start is past, a refresh skips the copy */

    Thread thread;
    volatile bool stop;         /* leaving the app: skip the images still to come */
    Catalog incoming;           /* the thread's result, until hub_poll() takes it */
    volatile bool has_incoming;
    Titles incoming_titles;
    volatile bool has_titles;
} Hub;

/* Starts (or restarts) loading; the screens see the result through hub_poll(). */
void hub_start(Hub *hub);
/* Called every frame: takes a finished catalog over. True when the catalog changed. */
bool hub_poll(Hub *hub);
void hub_exit(Hub *hub);

#endif
