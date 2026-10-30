#include "hub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog_key.h"
#include "core/fsutil.h"
#include "core/sha256.h"
#include "hublog.h"
#include "paths.h"
#include "store.h"

#define CATALOG_MAX (1024 * 1024) /* ~2 KB per entry: room for hundreds */
#define IMAGE_MAX (512 * 1024)
#define MAX_IMAGES 512
#define SLOW_START_MS 3000 /* a start that took longer leaves its timings in the journal */

typedef struct {
    char key[112];
    char sha[65];
} Wanted;

static void to_hex(const uint8_t digest[32], char out[65])
{
    for (int i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

bool hub_fetch_image(const char *key, const char *sha)
{
    char dest[160], path[160];
    store_image_path(dest, sizeof dest, sha);
    if (fs_exists(dest))
        return true;
    snprintf(path, sizeof path, "/v1/f/%s", key);
    char *body;
    size_t len;
    if (net_get(path, &body, &len, IMAGE_MAX, NULL, 0) != NET_OK)
        return false;
    uint8_t digest[32];
    char hex[65];
    sha256(body, len, digest);
    to_hex(digest, hex);
    bool ok = !strcmp(hex, sha) && fs_write_verified(dest, body, len, digest) == 0;
    free(body);
    return ok;
}

/* The copy on the card, if it still checks out. */
static bool load_cached(Catalog *cat)
{
    char sig[160] = "";
    char *body = NULL;
    size_t len = 0;
    bool ok = store_load_catalog(&body, &len, sig, sizeof sig) &&
              catalog_load(cat, body, len, sig, CATALOG_PUBLIC_KEY) == CAT_OK;
    free(body);
    return ok;
}

/* Gives the screens a catalog; they take one at a time (hub_poll). */
static void hand_over(Hub *hub, const Catalog *cat)
{
    while (hub->has_incoming && !hub->stop)
        svcSleepThread(10 * 1000 * 1000);
    if (hub->has_incoming) /* leaving: hub_poll() runs no more until this thread is joined */
        catalog_free(&hub->incoming);
    hub->incoming = *cat;
    hub->has_incoming = true;
}

static void load(void *arg)
{
    Hub *hub = arg;
    char sig[160] = "";
    char *body = NULL;
    size_t len = 0;
    Catalog cat;
    LoadState state = LOAD_NONE;

    /* At start, the copy on the card opens the catalog at once; the Worker's answer replaces it
     * when it comes (the bar says it is updating meanwhile). */
    bool cached = false;
    if (!hub->loaded_once && load_cached(&cat)) {
        store_catalog_time(hub->fetched_at, sizeof hub->fetched_at);
        hand_over(hub, &cat);
        cached = true;
        hub->early = true;
    }

    u64 began = osGetTime();
    hub->net = net_get("/v1/catalog", &body, &len, CATALOG_MAX, sig, sizeof sig);
    u64 fetched = osGetTime();
    if (hub->net == NET_OK) {
        CatResult r = catalog_load(&cat, body, len, sig, CATALOG_PUBLIC_KEY);
        if (r == CAT_OK) {
            store_save_catalog(body, len, sig);
            state = LOAD_ONLINE;
        } else {
            state = r == CAT_TOO_NEW ? LOAD_TOO_NEW : LOAD_BROKEN;
            snprintf(hub->need_app, sizeof hub->need_app, "%s", cat.need_app);
            catalog_free(&cat);
        }
    }
    free(body);

    /* no network, a broken answer, or a catalog this app cannot read (A4: the installed ones still
     * show from the last copy it could) */
    if (state != LOAD_ONLINE) {
        if (!cached && load_cached(&cat)) {
            hand_over(hub, &cat);
            cached = true;
        }
        if (cached && state != LOAD_TOO_NEW)
            state = LOAD_OFFLINE;
    }
    store_catalog_time(hub->fetched_at, sizeof hub->fetched_at);

    /* What to download next, copied: the catalog is the screens' once handed over. */
    static Wanted wanted[MAX_IMAGES];
    int n = 0;
    if (state == LOAD_ONLINE) {
        for (int i = 0; i < cat.n && n + 2 <= MAX_IMAGES; i++) {
            const CatFile *files[] = {&cat.items[i].icon, &cat.items[i].banner};
            for (int f = 0; f < 2; f++) {
                if (!files[f]->present)
                    continue;
                snprintf(wanted[n].key, sizeof wanted[n].key, "%s", files[f]->key);
                snprintf(wanted[n].sha, sizeof wanted[n].sha, "%s", files[f]->sha256);
                n++;
            }
        }
        hand_over(hub, &cat);
    }
    hub->images_left = n;
    hub->loaded_once = true;
    hub->state = state;

    /* The games on the console, after the screens open, in two passes: their title IDs at once (a
     * translation's page needs no more), then their names, icons and product codes, which take
     * seconds on an Old 3DS and only picking a game by hand needs. */
    u64 scan_began = osGetTime();
    titles_scan(&hub->incoming_titles, NULL, false, NULL);
    hub->has_titles = true;
    u64 scanned = osGetTime();

    for (int i = 0; i < n && !hub->stop; i++) {
        hub_fetch_image(wanted[i].key, wanted[i].sha);
        hub->images_left = n - i - 1;
    }

    while (hub->has_titles && !hub->stop) /* the screens take the first pass first */
        svcSleepThread(10 * 1000 * 1000);
    u64 details_began = osGetTime();
    Titles full = {0};
    titles_scan(&full, NULL, true, &hub->stop);
    u64 detailed = osGetTime();
    if (hub->has_titles) /* leaving: hub_poll() runs no more until this thread is joined */
        titles_free(&hub->incoming_titles);
    hub->incoming_titles = full;
    hub->has_titles = true;
    hub->scanning = false;

    if (scanned - began >= SLOW_START_MS) { /* what the screens wait for; the names come in the background */
        char detail[128];
        snprintf(detail, sizeof detail, "каталог %llu мс · ігор на консолі %d: пошук %llu мс, назви %llu мс", fetched - began,
                 full.n, scanned - scan_began, detailed - details_began);
        hublog_add("start", "3DS UA Hub", "ok", detail);
    }
}

void hub_start(Hub *hub)
{
    hub->stop = true; /* a refresh: the images of the last load can wait for this one */
    if (hub->thread) {
        threadJoin(hub->thread, U64_MAX);
        threadFree(hub->thread);
        hub->thread = NULL;
    }
    hub_poll(hub);
    hub->stop = false;
    hub->state = LOAD_RUNNING;
    hub->early = false;
    hub->scanning = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    /* below the screens, so drawing never waits on the network */
    hub->thread = threadCreate(load, hub, 0x8000, prio + 1, -2, false);
    if (!hub->thread)
        load(hub);
}

bool hub_poll(Hub *hub)
{
    if (hub->has_titles && hub->state != LOAD_RUNNING) {
        titles_free(&hub->titles);
        hub->titles = hub->incoming_titles;
        memset(&hub->incoming_titles, 0, sizeof hub->incoming_titles);
        hub->has_titles = false;
        hub->titles_ready = true;
        hub->titles_gen++;
    }
    if (!hub->has_incoming)
        return false;
    catalog_free(&hub->catalog);
    hub->catalog = hub->incoming;
    memset(&hub->incoming, 0, sizeof hub->incoming);
    hub->has_incoming = false;
    return true;
}

void hub_exit(Hub *hub)
{
    hub->stop = true;
    if (hub->thread) {
        threadJoin(hub->thread, U64_MAX);
        threadFree(hub->thread);
    }
    hub_poll(hub);
    catalog_free(&hub->catalog);
    titles_free(&hub->titles);
    titles_free(&hub->incoming_titles);
}
