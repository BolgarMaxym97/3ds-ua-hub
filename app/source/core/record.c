#include "record.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsutil.h"
#include "sha256.h"

int record_format(const Record *r, char **out, size_t *len)
{
    size_t cap = 512 + (size_t)r->n_files * 300;
    char *s = malloc(cap);
    if (!s)
        return -1;
    int n = snprintf(s, cap,
                     "slug=%s\nkind=%s\nversion=%s\nregion=%s\ntid=%016" PRIX64 "\nmanual=%d\nrisky=%d\ndate=%s\nluma=%d\n",
                     r->slug, r->kind, r->version, r->region, r->title_id, r->manual, r->risky, r->date, r->luma_changed);
    if (r->dir[0] && n > 0 && (size_t)n < cap)
        n += snprintf(s + n, cap - (size_t)n, "dir=%s\n", r->dir);
    for (int i = 0; i < r->n_files && n > 0 && (size_t)n < cap; i++)
        n += snprintf(s + n, cap - (size_t)n, "file %s %" PRIu32 " %s\n", r->files[i].sha256, r->files[i].size, r->files[i].path);
    if (n < 0 || (size_t)n >= cap) {
        free(s);
        return -1;
    }
    *out = s;
    *len = (size_t)n;
    return 0;
}

static void field(char *dst, size_t size, const char *value, size_t len)
{
    if (len >= size)
        len = size - 1;
    memcpy(dst, value, len);
    dst[len] = 0;
}

int record_parse(Record *r, const char *text)
{
    memset(r, 0, sizeof(*r));
    int cap = 0;
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        const char *eq = memchr(line, '=', len);
        if (len > 5 && !strncmp(line, "file ", 5)) {
            if (r->n_files == cap) {
                cap = cap ? cap * 2 : 16;
                RecFile *grown = realloc(r->files, (size_t)cap * sizeof(RecFile));
                if (!grown)
                    return -1;
                r->files = grown;
            }
            RecFile *f = &r->files[r->n_files];
            char path[256];
            unsigned long size;
            char tmp[512];
            field(tmp, sizeof(tmp), line, len);
            if (sscanf(tmp, "file %64s %lu %255[^\n]", f->sha256, &size, path) == 3) {
                f->size = (uint32_t)size;
                field(f->path, sizeof(f->path), path, strlen(path));
                r->n_files++;
            }
        } else if (eq) {
            size_t klen = (size_t)(eq - line);
            const char *v = eq + 1;
            size_t vlen = len - klen - 1;
#define KEY(name) (klen == strlen(name) && !strncmp(line, name, klen))
            if (KEY("slug"))
                field(r->slug, sizeof(r->slug), v, vlen);
            else if (KEY("kind"))
                field(r->kind, sizeof(r->kind), v, vlen);
            else if (KEY("version"))
                field(r->version, sizeof(r->version), v, vlen);
            else if (KEY("region"))
                field(r->region, sizeof(r->region), v, vlen);
            else if (KEY("tid"))
                r->title_id = strtoull(v, NULL, 16);
            else if (KEY("manual"))
                r->manual = *v == '1';
            else if (KEY("risky"))
                r->risky = *v == '1';
            else if (KEY("date"))
                field(r->date, sizeof(r->date), v, vlen);
            else if (KEY("luma"))
                r->luma_changed = *v == '1';
            else if (KEY("dir"))
                field(r->dir, sizeof(r->dir), v, vlen);
#undef KEY
        }
        line += len + (end ? 1 : 0);
    }
    return r->slug[0] && r->title_id ? 0 : -1;
}

void record_free(Record *r)
{
    free(r->files);
    r->files = NULL;
    r->n_files = 0;
}

static void record_path(char *out, size_t size, const char *root, const char *slug)
{
    snprintf(out, size, "%s" HUB_SUBDIR "/installed/%s.txt", root, slug);
}

int record_save(const char *root, const Record *r)
{
    char path[256], *text;
    size_t len;
    uint8_t sha[32];
    record_path(path, sizeof(path), root, r->slug);
    if (record_format(r, &text, &len))
        return -1;
    sha256(text, len, sha);
    int status = fs_replace_verified(path, text, len, sha);
    free(text);
    return status;
}

int record_delete(const char *root, const char *slug)
{
    char path[256];
    record_path(path, sizeof(path), root, slug);
    return fs_exists(path) ? fs_remove(path) : 0;
}

typedef struct {
    const char *dir;
    Record *list;
    int n, cap;
} Loader;

static void load_one(const char *name, void *ctx)
{
    Loader *l = ctx;
    size_t n = strlen(name);
    if (n < 5 || strcmp(name + n - 4, ".txt"))
        return;
    char path[512];
    size_t dir_len = strlen(l->dir);
    if (dir_len + 1 + n >= sizeof(path))
        return;
    memcpy(path, l->dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, name, n + 1);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    const size_t max = 1024 * 1024; /* on the heap: a thread's stack on the console is small */
    char *text = malloc(max);
    size_t got = text ? fread(text, 1, max - 1, f) : 0;
    fclose(f);
    if (!text)
        return;
    text[got] = 0;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        Record *grown = realloc(l->list, (size_t)l->cap * sizeof(Record));
        if (!grown) {
            free(text);
            return;
        }
        l->list = grown;
    }
    if (record_parse(&l->list[l->n], text) == 0)
        l->n++;
    else
        record_free(&l->list[l->n]);
    free(text);
}

int record_load_all(const char *root, Record **out, int *n)
{
    char dir[256];
    snprintf(dir, sizeof(dir), "%s" HUB_SUBDIR "/installed", root);
    Loader l = {dir, NULL, 0, 0};
    if (fs_is_dir(dir))
        fs_list(dir, load_one, &l);
    *out = l.list;
    *n = l.n;
    return 0;
}
