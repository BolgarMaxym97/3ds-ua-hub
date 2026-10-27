/*
 * The public half of the key that signs the catalog (worker: `npm run keygen`, docs/DEPLOY.md).
 * The private half is the Worker's SIGNING_KEY secret. A new key needs a new app release: older
 * apps then refuse the catalog.
 */
#ifndef CATALOG_KEY_H
#define CATALOG_KEY_H

#include <stdint.h>

/* A test build against a local Worker passes its own key: -DCATALOG_KEY_BYTES=0x..,0x.. */
#ifndef CATALOG_KEY_BYTES
#define CATALOG_KEY_BYTES                                                                                              \
    0xe3, 0x03, 0xef, 0x54, 0x6a, 0x1a, 0x22, 0x0e, 0xf7, 0xf8, 0xc6, 0xbc, 0x4c, 0xb4, 0x11, 0x41, 0x30, 0x4c, 0x73,  \
        0x6b, 0x01, 0x28, 0xff, 0xab, 0x86, 0xe3, 0x27, 0xad, 0x34, 0xbd, 0x6b, 0x79
#endif

static const uint8_t CATALOG_PUBLIC_KEY[32] = {CATALOG_KEY_BYTES};

#endif
