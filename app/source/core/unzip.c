#include "unzip.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <zlib.h>

#include "fsutil.h"
#include "sha256.h"

#define CHUNK 0x8000

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Entry names become paths under luma/titles/<TID>/: relative, no "..", no backslashes. */
static bool safe_name(const char *s)
{
    if (!*s || s[0] == '/' || strchr(s, '\\') || strchr(s, ':'))
        return false;
    for (const char *p = s; *p;) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if ((len == 2 && p[0] == '.' && p[1] == '.') || (len == 1 && p[0] == '.'))
            return false;
        p += len + (end ? 1 : 0);
    }
    return true;
}

int zip_open(Zip *z, const char *path)
{
    memset(z, 0, sizeof(*z));
    if (!(z->f = fopen(path, "rb")))
        return -1;
    /* the end-of-central-directory record: within the last 64 KB + 22 bytes */
    fseek(z->f, 0, SEEK_END);
    long size = ftell(z->f);
    long tail = size < 0x10000 + 22 ? size : 0x10000 + 22;
    uint8_t *buf = malloc((size_t)tail);
    if (!buf || tail < 22)
        goto fail;
    fseek(z->f, size - tail, SEEK_SET);
    if (fread(buf, 1, (size_t)tail, z->f) != (size_t)tail)
        goto fail;
    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--)
        if (u32(buf + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        goto fail;
    uint16_t count = u16(buf + eocd + 10);
    uint32_t cd_size = u32(buf + eocd + 12), cd_offset = u32(buf + eocd + 16);
    free(buf);
    buf = NULL;
    if (count > ZIP_MAX_ENTRIES || (long)cd_offset + (long)cd_size > size)
        goto fail;

    uint8_t *cd = malloc(cd_size ? cd_size : 1);
    z->entries = calloc(count ? count : 1, sizeof(ZipEntry));
    if (!cd || !z->entries) {
        free(cd);
        goto fail;
    }
    fseek(z->f, cd_offset, SEEK_SET);
    if (fread(cd, 1, cd_size, z->f) != cd_size) {
        free(cd);
        goto fail;
    }
    uint32_t at = 0;
    for (int i = 0; i < count; i++) {
        if (at + 46 > cd_size || u32(cd + at) != 0x02014b50)
            break;
        uint16_t name_len = u16(cd + at + 28), extra = u16(cd + at + 30), comment = u16(cd + at + 32);
        if (at + 46 + name_len > cd_size || name_len >= sizeof(z->entries[0].name))
            break;
        ZipEntry e = {0};
        memcpy(e.name, cd + at + 46, name_len);
        e.method = u16(cd + at + 10);
        e.crc = u32(cd + at + 16);
        e.csize = u32(cd + at + 20);
        e.usize = u32(cd + at + 24);
        e.offset = u32(cd + at + 42);
        at += 46 + name_len + extra + comment;
        if (name_len && e.name[name_len - 1] == '/')
            continue; /* a folder: made as its files need it */
        if (!safe_name(e.name) || (e.method != 0 && e.method != 8) || e.usize > ZIP_MAX_FILE) {
            free(cd);
            goto fail;
        }
        z->entries[z->n++] = e;
    }
    free(cd);
    return 0;
fail:
    free(buf);
    zip_close(z);
    return -1;
}

void zip_close(Zip *z)
{
    if (z->f)
        fclose(z->f);
    free(z->entries);
    memset(z, 0, sizeof(*z));
}

int zip_find(const Zip *z, const char *name)
{
    for (int i = 0; i < z->n; i++)
        if (!strcmp(z->entries[i].name, name))
            return i;
    return -1;
}

typedef int (*Sink)(const uint8_t *data, size_t len, void *ctx);

/* Inflates (or copies) entry `i` chunk by chunk into `sink`, checking size and CRC. */
static int read_entry(const Zip *z, int i, Sink sink, void *ctx)
{
    const ZipEntry *e = &z->entries[i];
    uint8_t local[30];
    fseek(z->f, e->offset, SEEK_SET);
    if (fread(local, 1, 30, z->f) != 30 || u32(local) != 0x04034b50)
        return -1;
    fseek(z->f, e->offset + 30 + u16(local + 26) + u16(local + 28), SEEK_SET);

    uint8_t *in = malloc(CHUNK), *out = malloc(CHUNK);
    int status = -1;
    uint32_t crc = crc32(0, NULL, 0), produced = 0, left = e->csize;
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (!in || !out || (e->method == 8 && inflateInit2(&s, -MAX_WBITS) != Z_OK))
        goto done;
    int zr = Z_OK;
    while (left > 0 && zr != Z_STREAM_END) {
        size_t want = left < CHUNK ? left : CHUNK;
        if (fread(in, 1, want, z->f) != want)
            goto done_inflate;
        left -= (uint32_t)want;
        if (e->method == 0) {
            if (sink(in, want, ctx)) {
                status = ZIP_WRITE_ERROR;
                goto done_inflate;
            }
            crc = crc32(crc, in, (uInt)want);
            produced += (uint32_t)want;
            continue;
        }
        s.next_in = in;
        s.avail_in = (uInt)want;
        do {
            s.next_out = out;
            s.avail_out = CHUNK;
            zr = inflate(&s, Z_NO_FLUSH);
            /* the last call filled the output just as the input ran out: nothing more to do until
             * the next chunk (Z_BUF_ERROR), not damage */
            if (zr == Z_BUF_ERROR && s.avail_in == 0) {
                zr = Z_OK;
                break;
            }
            if (zr != Z_OK && zr != Z_STREAM_END)
                goto done_inflate;
            size_t n = CHUNK - s.avail_out;
            produced += (uint32_t)n;
            if (produced > e->usize)
                goto done_inflate;
            if (n && sink(out, n, ctx)) {
                status = ZIP_WRITE_ERROR;
                goto done_inflate;
            }
            crc = crc32(crc, out, (uInt)n);
        } while (s.avail_out == 0 && zr != Z_STREAM_END);
    }
    status = produced == e->usize && crc == e->crc ? 0 : -1;
done_inflate:
    if (e->method == 8)
        inflateEnd(&s);
done:
    free(in);
    free(out);
    return status;
}

typedef struct {
    FILE *f;
    Sha256 sha;
    volatile uint64_t *written;
} FileSink;

static int to_file(const uint8_t *data, size_t len, void *ctx)
{
    FileSink *fs = ctx;
    sha256_update(&fs->sha, data, len);
    if (fwrite(data, 1, len, fs->f) != len)
        return -1;
    if (fs->written)
        *fs->written += len;
    return 0;
}

/* Writing big files on the console: a file that grows by every 32 KB write makes the FS find
 * clusters each time (Undertale's 72 MB atlas took over ten minutes on an Old 3DS, 2026-10-26).
 * The whole size is set first and the writes go out in large pieces. */
#define WRITE_BUFFER (256 * 1024)

int zip_extract(const Zip *z, int i, const char *dest, uint8_t sha[32])
{
    if (fs_make_parents(dest))
        return ZIP_WRITE_ERROR;
    FileSink fs = {.written = z->written};
    if (!(fs.f = fopen(dest, "wb")))
        return ZIP_WRITE_ERROR;
    char *buffer = z->entries[i].usize > CHUNK ? malloc(WRITE_BUFFER) : NULL;
    if (buffer)
        setvbuf(fs.f, buffer, _IOFBF, WRITE_BUFFER);
    if (z->entries[i].usize > CHUNK && ftruncate(fileno(fs.f), z->entries[i].usize) == 0)
        rewind(fs.f); /* the clusters are there; the writes only fill them */
    sha256_init(&fs.sha);
    int r = read_entry(z, i, to_file, &fs);
    if (fflush(fs.f) || fclose(fs.f))
        r = ZIP_WRITE_ERROR;
    free(buffer);
    if (r == 0 && sha)
        sha256_final(&fs.sha, sha);
    return r;
}

typedef struct {
    char *buf;
    size_t len, cap;
} MemSink;

static int to_memory(const uint8_t *data, size_t len, void *ctx)
{
    MemSink *m = ctx;
    if (m->len + len + 1 > m->cap)
        return -1;
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    return 0;
}

char *zip_read(const Zip *z, int i, size_t *len)
{
    MemSink m = {malloc(z->entries[i].usize + 1), 0, z->entries[i].usize + 1};
    if (!m.buf || read_entry(z, i, to_memory, &m)) {
        free(m.buf);
        return NULL;
    }
    m.buf[m.len] = 0;
    if (len)
        *len = m.len;
    return m.buf;
}
