/* Where the Hub talks to and what it keeps on the SD card. */
#ifndef PATHS_H
#define PATHS_H

/* The Worker (worker/, docs/DEPLOY.md). Plain HTTP: the console's TLS cannot reach workers.dev;
 * the catalog is signed and every file checked against it instead. Changing it needs a release. */
#ifndef HUB_URL /* a test build may point elsewhere: make app EXTRA_CFLAGS=-DHUB_URL=... */
#define HUB_URL "http://3ds-ua-hub.bolgar-maxym.workers.dev"
#endif

#define HUB_DIR "sdmc:/3ds/3ds-ua-hub"
#define CACHE_DIR HUB_DIR "/cache"
#define CATALOG_CACHE CACHE_DIR "/catalog.json"
#define CATALOG_CACHE_SIG CACHE_DIR "/catalog.sig"
#define CATALOG_CACHE_TIME CACHE_DIR "/catalog.time" /* when it was fetched: "2026-10-25 14:02" */
#define IMAGE_CACHE CACHE_DIR "/img"                  /* <sha256>.png: banners and icons */

#endif
