#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"

#define CHUNK 0x4000
#define CONNECT_TIMEOUT_NS (10ull * 1000 * 1000 * 1000)
#define READ_TIMEOUT_NS (15ull * 1000 * 1000 * 1000)

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

void net_init(void)
{
    /* 0x1000: room for a stats POST body later; a GET needs none. */
    httpcInit(0x1000);
}

void net_exit(void)
{
    httpcExit();
}

typedef NetSink Sink;

/* One GET; the body goes to `sink` chunk by chunk. `from` > 0 asks for the bytes from there on. */
static NetResult fetch(const char *path, u32 from, Sink sink, void *ctx, char *sig, size_t sig_size, NetProgress *progress)
{
    char url[256];
    snprintf(url, sizeof url, "%s%s", HUB_URL, path);
    httpcContext http;
    Result failed = httpcOpenContext(&http, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(failed)) {
        if (progress)
            progress->result = failed;
        return NET_FAILED;
    }
    httpcSetKeepAlive(&http, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&http, "User-Agent", "3ds-ua-hub/" APP_VERSION);
    char range[32];
    if (from) {
        snprintf(range, sizeof range, "bytes=%lu-", (unsigned long)from);
        httpcAddRequestHeaderField(&http, "Range", range);
    }

    NetResult result = NET_OK;
    u32 status = 0;
    if (R_FAILED(failed = httpcBeginRequest(&http)) ||
        R_FAILED(failed = httpcGetResponseStatusCodeTimeout(&http, &status, CONNECT_TIMEOUT_NS)) ||
        status < 100 || status > 599) { /* no HTTP answer at all (Azahar, the server unreachable, gives
                                          * 0xFFFFFFFF): no connection, not the server's error */
        result = NET_FAILED;
    } else if (status == 404) {
        result = NET_NOT_FOUND;
    } else if (status == 416) {
        result = NET_OK; /* asked from the end: everything is already here */
    } else if (status != 200 && status != 206) {
        result = NET_SERVER;
    } else if (from && status == 200) {
        result = NET_SERVER; /* the range was ignored: appending would corrupt the file */
    } else {
        if (sig && sig_size && R_FAILED(httpcGetResponseHeader(&http, "X-Signature", sig, sig_size)))
            sig[0] = 0;
        u32 total = 0;
        httpcGetDownloadSizeState(&http, NULL, &total);
        if (progress)
            progress->total = from + total;
        u8 *buf = malloc(CHUNK);
        if (!buf)
            result = NET_NO_MEMORY;
        for (Result r = HTTPC_RESULTCODE_DOWNLOADPENDING; buf && r == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING;) {
            if (progress && progress->cancel) {
                result = NET_CANCELLED;
                break;
            }
            u32 before = 0, after = 0;
            httpcGetDownloadSizeState(&http, &before, NULL);
            r = httpcReceiveDataTimeout(&http, buf, CHUNK, READ_TIMEOUT_NS);
            httpcGetDownloadSizeState(&http, &after, NULL);
            if (r != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING && R_FAILED(r)) {
                failed = r;
                result = NET_LOST;
                break;
            }
            if (after > before && !sink(buf, after - before, ctx)) {
                result = NET_WRITE;
                break;
            }
            if (progress)
                progress->done = from + after;
        }
        free(buf);
    }
    if (progress) {
        progress->status = status;
        progress->result = R_FAILED(failed) ? failed : 0;
    }
    httpcCloseContext(&http);
    return result;
}

/* ---------------------------------------------------------------- into memory */

typedef struct {
    char *data;
    size_t len, cap, max;
    bool too_big;
} Buffer;

static bool to_buffer(const u8 *data, u32 len, void *ctx)
{
    Buffer *b = ctx;
    if (b->len + len > b->max) {
        b->too_big = true;
        return false;
    }
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 0x8000;
        while (cap < b->len + len + 1)
            cap *= 2;
        char *grown = realloc(b->data, cap);
        if (!grown)
            return false;
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
    return true;
}

NetResult net_get(const char *path, char **body, size_t *len, size_t max, char *sig, size_t sig_size)
{
    Buffer b = {.max = max};
    NetResult r = fetch(path, 0, to_buffer, &b, sig, sig_size, NULL);
    if (r == NET_WRITE)
        r = b.too_big ? NET_TOO_BIG : NET_NO_MEMORY;
    if (r == NET_OK && !b.data)
        r = to_buffer((const u8 *)"", 0, &b) ? NET_OK : NET_NO_MEMORY;
    if (r != NET_OK) {
        free(b.data);
        return r;
    }
    *body = b.data;
    *len = b.len;
    return NET_OK;
}

NetResult net_stream(const char *path, NetSink sink, void *ctx, NetProgress *progress)
{
    return fetch(path, 0, sink, ctx, NULL, 0, progress);
}

/* ---------------------------------------------------------------- onto the card */

static bool to_file(const u8 *data, u32 len, void *ctx)
{
    return fwrite(data, 1, len, (FILE *)ctx) == len;
}

NetResult net_download(const char *path, const char *dest, u32 size, NetProgress *progress)
{
    FILE *f = fopen(dest, "ab");
    if (!f)
        return NET_WRITE;
    long have = ftell(f);
    if (have < 0 || (u32)have > size) {
        /* more than the file should be: start over */
        fclose(f);
        if (!(f = fopen(dest, "wb")))
            return NET_WRITE;
        have = 0;
    }
    if (progress) {
        progress->done = (u32)have;
        progress->total = size;
    }
    NetResult r = (u32)have == size ? NET_OK : fetch(path, (u32)have, to_file, f, NULL, 0, progress);
    if (fclose(f) != 0 && r == NET_OK)
        r = NET_WRITE;
    if (r == NET_OK) {
        FILE *check = fopen(dest, "rb");
        long got = -1;
        if (check) {
            fseek(check, 0, SEEK_END);
            got = ftell(check);
            fclose(check);
        }
        if (got != (long)size)
            r = NET_LOST;
    }
    return r;
}

/* ---------------------------------------------------------------- onto the card, hashed */

typedef struct {
    FILE *f;
    Sha256 *sha;
} Hashed;

static bool to_hashed_file(const u8 *data, u32 len, void *ctx)
{
    Hashed *h = ctx;
    if (fwrite(data, 1, len, h->f) != len)
        return false;
    sha256_update(h->sha, data, len);
    return true;
}

/* The part already on the card, through the hash; its length, or -1 if it cannot be read. */
static long hash_part(const char *dest, u32 size, Sha256 *sha, NetProgress *progress)
{
    FILE *f = fopen(dest, "rb");
    if (!f)
        return 0; /* nothing yet */
    u8 *buf = malloc(CHUNK * 4);
    long have = 0;
    size_t n;
    while (buf && (n = fread(buf, 1, CHUNK * 4, f)) > 0 && (u32)have + n <= size) {
        sha256_update(sha, buf, n);
        have += (long)n;
        if (progress)
            progress->done = (u32)have;
    }
    bool longer = buf && !feof(f); /* more than the file should be */
    free(buf);
    fclose(f);
    return !buf || longer ? -1 : have;
}

NetResult net_download_hashed(const char *path, const char *dest, u32 size, Sha256 *sha, NetProgress *progress)
{
    sha256_init(sha);
    if (progress)
        progress->total = size;
    long have = hash_part(dest, size, sha, progress);
    FILE *f = fopen(dest, have < 0 ? "wb" : "ab");
    if (!f)
        return NET_WRITE;
    if (have < 0) { /* unreadable or too long: start over */
        sha256_init(sha);
        have = 0;
    }
    Hashed h = {f, sha};
    NetResult r = (u32)have == size ? NET_OK : fetch(path, (u32)have, to_hashed_file, &h, NULL, 0, progress);
    if (r == NET_SERVER && have > 0 && progress && progress->status == 200) {
        /* the server sends the whole file whatever was asked: take it from the start */
        fclose(f);
        if (!(f = fopen(dest, "wb")))
            return NET_WRITE;
        sha256_init(sha);
        h.f = f;
        r = fetch(path, 0, to_hashed_file, &h, NULL, 0, progress);
    }
    if (fclose(f) != 0 && r == NET_OK)
        r = NET_WRITE;
    if (r == NET_OK && progress && progress->done != size)
        r = NET_LOST;
    return r;
}

/* ---------------------------------------------------------------- HEAD */

NetResult net_head(const char *path, u32 *size, u32 *status, char *reason, size_t reason_size)
{
    char url[256];
    snprintf(url, sizeof url, "%s%s", HUB_URL, path);
    *size = *status = 0;
    if (reason_size)
        reason[0] = 0;
    httpcContext http;
    if (R_FAILED(httpcOpenContext(&http, HTTPC_METHOD_HEAD, url, 1)))
        return NET_FAILED;
    httpcAddRequestHeaderField(&http, "User-Agent", "3ds-ua-hub/" APP_VERSION);
    NetResult result;
    /* the Worker asks the game's server first: give it the time it gives that one, and more */
    if (R_FAILED(httpcBeginRequest(&http)) || R_FAILED(httpcGetResponseStatusCodeTimeout(&http, status, 3 * CONNECT_TIMEOUT_NS)) ||
        *status < 100 || *status > 599) {
        *status = 0;
        result = NET_FAILED;
    } else if (*status == 200) {
        char length[16] = "";
        httpcGetResponseHeader(&http, "Content-Length", length, sizeof length);
        *size = (u32)strtoul(length, NULL, 10);
        result = NET_OK;
    } else {
        if (reason_size && R_FAILED(httpcGetResponseHeader(&http, "X-Fallback-Error", reason, reason_size)))
            reason[0] = 0;
        result = *status == 404 ? NET_NOT_FOUND : NET_SERVER;
    }
    httpcCloseContext(&http);
    return result;
}
