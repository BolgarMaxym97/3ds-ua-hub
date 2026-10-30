#include "shots.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "core/fsutil.h"
#include "hub.h"
#include "store.h"

typedef struct {
    char key[112];
    char sha[65];
    volatile ShotState state;
} Wanted;

static LightLock lock;
static bool lock_ready;
static Wanted want[CAT_MAX_SHOTS];
static int n_want;
static int gen;               /* counts shots_want(): a result for an older page is dropped */
static bool running, stop;
static Thread thread;

/* The next one to fetch, copied out, or false when there is none: the thread then ends. */
static bool next(Wanted *out, int *index, int *at_gen)
{
    LightLock_Lock(&lock);
    bool found = false;
    for (int i = 0; i < n_want && !stop; i++) {
        if (want[i].state == SHOT_LOADING) {
            *out = want[i];
            *index = i;
            *at_gen = gen;
            found = true;
            break;
        }
    }
    if (!found)
        running = false;
    LightLock_Unlock(&lock);
    return found;
}

static void fetch(void *arg)
{
    (void)arg;
    Wanted w;
    int i, at_gen;
    while (next(&w, &i, &at_gen)) {
        ShotState got = hub_fetch_image(w.key, w.sha) ? SHOT_READY : SHOT_FAILED;
        LightLock_Lock(&lock);
        if (gen == at_gen)
            want[i].state = got;
        LightLock_Unlock(&lock);
    }
}

void shots_want(const CatItem *it)
{
    if (!lock_ready) {
        LightLock_Init(&lock);
        lock_ready = true;
    }
    LightLock_Lock(&lock);
    gen++;
    n_want = it->n_shots;
    for (int i = 0; i < n_want; i++) {
        snprintf(want[i].key, sizeof want[i].key, "%s", it->shots[i].file.key);
        snprintf(want[i].sha, sizeof want[i].sha, "%s", it->shots[i].file.sha256);
        char path[160];
        store_image_path(path, sizeof path, want[i].sha);
        want[i].state = fs_exists(path) ? SHOT_READY : SHOT_LOADING;
    }
    bool pending = false;
    for (int i = 0; i < n_want; i++)
        pending = pending || want[i].state == SHOT_LOADING;
    bool start = pending && !running && !stop;
    if (start)
        running = true;
    LightLock_Unlock(&lock);
    if (!start)
        return;
    if (thread) { /* the last one has finished, or is about to: it found nothing more */
        threadJoin(thread, U64_MAX);
        threadFree(thread);
    }
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    /* below the screens, like the catalog's thread */
    thread = threadCreate(fetch, NULL, 0x4000, prio + 1, -2, false);
    if (!thread)
        running = false;
}

ShotState shots_state(const char *sha)
{
    if (!lock_ready)
        return SHOT_LOADING;
    ShotState state = SHOT_LOADING;
    LightLock_Lock(&lock);
    for (int i = 0; i < n_want; i++)
        if (!strcmp(want[i].sha, sha))
            state = want[i].state;
    LightLock_Unlock(&lock);
    return state;
}

void shots_exit(void)
{
    if (!lock_ready)
        return;
    LightLock_Lock(&lock);
    stop = true;
    LightLock_Unlock(&lock);
    if (thread) {
        threadJoin(thread, U64_MAX);
        threadFree(thread);
        thread = NULL;
    }
}
