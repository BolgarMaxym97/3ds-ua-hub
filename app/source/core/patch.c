#include "patch.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsutil.h"
#include "sha256.h"
#include "unzip.h"

#define PATH_MAX_LEN 320

typedef struct {
    char live[PATH_MAX_LEN - 16]; /* <root>/luma/titles/<TID> (a CIA's data: <root>/<dir>), room left for the suffixes below */
    char fresh[PATH_MAX_LEN];   /* ...<TID>.hub-new: being unpacked */
    char old[PATH_MAX_LEN];     /* ...<TID>.hub-old: the set the swap replaced */
    char gone[PATH_MAX_LEN];    /* ...<TID>.hub-del: being removed */
    char backup[PATH_MAX_LEN];  /* <root>/3ds/3ds-ua-hub/backup/<TID> */
    char pending[PATH_MAX_LEN]; /* <root>/3ds/3ds-ua-hub/pending.txt */
} Paths;

static void paths(Paths *p, const char *root, const Record *rec)
{
    uint64_t tid = rec->title_id;
    if (rec->dir[0])
        snprintf(p->live, sizeof(p->live), "%s/%s", root, rec->dir);
    else
        snprintf(p->live, sizeof(p->live), "%s/luma/titles/%016" PRIX64, root, tid);
    snprintf(p->fresh, sizeof(p->fresh), "%s.hub-new", p->live);
    snprintf(p->old, sizeof(p->old), "%s.hub-old", p->live);
    snprintf(p->gone, sizeof(p->gone), "%s.hub-del", p->live);
    snprintf(p->backup, sizeof(p->backup), "%s" HUB_SUBDIR "/backup/%016" PRIX64, root, tid);
    snprintf(p->pending, sizeof(p->pending), "%s" HUB_SUBDIR "/pending.txt", root);
}

/* pending.txt: "op=..", "stage=..", then the record the operation is about. */
static int write_pending(const Paths *p, const char *op, const char *stage, const Record *rec)
{
    char *text;
    size_t len;
    if (record_format(rec, &text, &len))
        return -1;
    size_t cap = len + 64;
    char *all = malloc(cap);
    if (!all) {
        free(text);
        return -1;
    }
    int n = snprintf(all, cap, "op=%s\nstage=%s\n%s", op, stage, text);
    uint8_t sha[32];
    sha256(all, (size_t)n, sha);
    int status = fs_replace_verified(p->pending, all, (size_t)n, sha);
    free(text);
    free(all);
    return status;
}

/* ---------------------------------------------------------------- other mods' files */

typedef struct {
    const Record *ours;     /* the Hub's earlier install in this folder, or NULL */
    const Paths *p;
    int failed;
} Foreign;

static int is_ours(const Record *r, const char *rel)
{
    for (int i = 0; r && i < r->n_files; i++)
        if (!strcmp(r->files[i].path, rel))
            return 1;
    return 0;
}

static int keep_foreign(const char *rel, void *ctx)
{
    Foreign *f = ctx;
    if (is_ours(f->ours, rel))
        return 0;
    char from[PATH_MAX_LEN * 2], to[PATH_MAX_LEN * 2];
    snprintf(from, sizeof(from), "%s/%s", f->p->live, rel);
    snprintf(to, sizeof(to), "%s/%s", f->p->backup, rel);
    if (fs_exists(to))
        return 0; /* the first copy is the original */
    if (fs_copy(from, to))
        f->failed = 1;
    return 0;
}

/* A CIA's data folder: files that are not the Hub's (the game's saves) go along into the new set. */
static int carry_foreign(const char *rel, void *ctx)
{
    Foreign *f = ctx;
    if (is_ours(f->ours, rel))
        return 0;
    char from[PATH_MAX_LEN * 2], to[PATH_MAX_LEN * 2];
    snprintf(from, sizeof(from), "%s/%s", f->p->live, rel);
    snprintf(to, sizeof(to), "%s/%s", f->p->fresh, rel);
    if (!fs_exists(to) && fs_copy(from, to)) /* the archive's own file wins */
        f->failed = 1;
    return 0;
}

typedef struct {
    const Paths *p;
    int failed;
} Restore;

static int restore_one(const char *rel, void *ctx)
{
    Restore *r = ctx;
    char from[PATH_MAX_LEN * 2], to[PATH_MAX_LEN * 2];
    snprintf(from, sizeof(from), "%s/%s", r->p->backup, rel);
    snprintf(to, sizeof(to), "%s/%s", r->p->live, rel);
    if (!fs_exists(to) && fs_copy(from, to))
        r->failed = 1;
    return 0;
}

/* ---------------------------------------------------------------- install */

static Record *previous_install(const char *root, const Record *rec, Record *store)
{
    uint64_t tid = rec->title_id;
    Record *all;
    int n;
    Record *found = NULL;
    record_load_all(root, &all, &n);
    for (int i = 0; i < n; i++) {
        bool same = rec->dir[0] ? !strcmp(all[i].dir, rec->dir)
                                : all[i].title_id == tid && !strcmp(all[i].kind, "patch");
        if (!found && same) {
            *store = all[i];
            found = store; /* any of the Hub's installs in this folder counts as ours */
            continue;
        }
        record_free(&all[i]);
    }
    free(all);
    return found;
}

PatchResult patch_install(const char *root, const char *zip_path, Record *rec, PatchProgress *progress)
{
    Paths p;
    paths(&p, root, rec);
    Zip zip;
    if (zip_open(&zip, zip_path))
        return PATCH_BAD_ARCHIVE;

    if (progress) {
        progress->stage = PSTAGE_EXTRACT;
        progress->done = 0;
        progress->total = (uint32_t)zip.n;
        progress->bytes = progress->bytes_total = 0;
        for (int i = 0; i < zip.n; i++)
            progress->bytes_total += zip.entries[i].usize;
        zip.written = &progress->bytes;
    }
    for (int i = 0; i < zip.n; i++) {
        if (strlen(zip.entries[i].name) >= sizeof(rec->files->path)) { /* the record could not name it */
            zip_close(&zip);
            return PATCH_BAD_ARCHIVE;
        }
    }
    record_free(rec);
    rec->files = calloc(zip.n ? (size_t)zip.n : 1, sizeof(RecFile));
    if (!rec->files) {
        zip_close(&zip);
        return PATCH_WRITE;
    }

    PatchResult result = PATCH_WRITE;
    if (fs_remove_tree(p.fresh) || fs_remove_tree(p.old) || write_pending(&p, "install", "unpack", rec))
        goto out;
    for (int i = 0; i < zip.n; i++) {
        if (!strcmp(zip.entries[i].name, "check.txt"))
            continue; /* for the check before installing, not for the game */
        char dest[PATH_MAX_LEN * 2];
        snprintf(dest, sizeof(dest), "%s/%s", p.fresh, zip.entries[i].name);
        uint8_t sha[32];
        int r = zip_extract(&zip, i, dest, sha);
        if (r) {
            result = r == ZIP_WRITE_ERROR ? PATCH_WRITE : PATCH_BAD_ARCHIVE;
            goto out;
        }
        RecFile *f = &rec->files[rec->n_files++];
        for (int b = 0; b < 32; b++)
            snprintf(f->sha256 + 2 * b, 3, "%02x", sha[b]);
        f->size = zip.entries[i].usize;
        memcpy(f->path, zip.entries[i].name, strlen(zip.entries[i].name) + 1); /* fits: checked above */
        if (progress)
            progress->done = (uint32_t)i + 1;
    }

    if (progress)
        progress->stage = PSTAGE_BACKUP;
    Record prev_store, *prev = previous_install(root, rec, &prev_store);
    if (fs_is_dir(p.live)) {
        Foreign f = {prev, &p, 0};
        fs_walk(p.live, rec->dir[0] ? carry_foreign : keep_foreign, &f);
        if (f.failed) {
            if (prev)
                record_free(prev);
            goto out;
        }
    }
    if (progress)
        progress->stage = PSTAGE_SWAP;
    /* the swap: after the second rename the game sees the new set. "swapping" first, so a cut
     * between the renames is told apart from one before them (when the live folder is still the
     * other mod's own and must not be touched) */
    if (write_pending(&p, "install", "swapping", rec) || (fs_is_dir(p.live) && fs_rename(p.live, p.old)) ||
        fs_rename(p.fresh, p.live)) {
        if (prev)
            record_free(prev);
        goto out;
    }
    if (write_pending(&p, "install", "swapped", rec) == 0 && fs_remove_tree(p.old) == 0) {
        /* an update of another slug in the same folder: that record is replaced by this one */
        if (prev && strcmp(prev->slug, rec->slug))
            record_delete(root, prev->slug);
        if (record_save(root, rec) == 0 && fs_remove(p.pending) == 0)
            result = PATCH_OK;
    }
    if (prev)
        record_free(prev);
out:
    zip_close(&zip);
    if (result != PATCH_OK)
        patch_recover(root, NULL, 0); /* back to the old set, unless the swap is done */
    if (progress && result == PATCH_OK)
        progress->stage = PSTAGE_DONE;
    return result;
}

/* ---------------------------------------------------------------- remove */

/* Removal in three stages, each safe to repeat from the start of its stage:
 *   start    - the live folder is ours: it goes aside to <TID>.hub-del;
 *   cleared  - ours is gone; the other mod's files are copied back (only the missing ones);
 *   restored - all back: the backup and the record go. */
/* A CIA's data: only the Hub's own files go, one by one (repeating is harmless); the game's saves
 * and anything else in the folder stay. */
static void count_files(PatchProgress *progress, const Record *rec)
{
    if (!progress)
        return;
    progress->done = 0;
    progress->total = (uint32_t)rec->n_files;
    progress->bytes = progress->bytes_total = 0; /* by files: removing one takes no longer for its size */
}

static PatchResult remove_data(const char *root, const Paths *p, const Record *rec, PatchProgress *progress)
{
    char stop[PATH_MAX_LEN];
    snprintf(stop, sizeof(stop), "%s/3ds", root);
    count_files(progress, rec);
    for (int i = 0; i < rec->n_files; i++) {
        char path[PATH_MAX_LEN * 2];
        snprintf(path, sizeof(path), "%s/%s", p->live, rec->files[i].path);
        if (fs_remove(path) && fs_exists(path))
            return PATCH_WRITE;
        char *slash = strrchr(path, '/');
        *slash = 0;
        fs_prune_empty(path, stop);
        if (progress)
            progress->done = (uint32_t)i + 1;
    }
    if (record_delete(root, rec->slug))
        return PATCH_WRITE;
    return fs_remove(p->pending) ? PATCH_WRITE : PATCH_OK;
}

static PatchResult finish_remove(const char *root, const Paths *p, const Record *rec, const char *stage,
                                 PatchProgress *progress)
{
    if (rec->dir[0])
        return remove_data(root, p, rec, progress);
    if (!strcmp(stage, "start")) {
        if (fs_is_dir(p->live) && !fs_is_dir(p->gone) && fs_rename(p->live, p->gone))
            return PATCH_WRITE;
        if (write_pending(p, "remove", "cleared", rec))
            return PATCH_WRITE;
        stage = "cleared";
    }
    if (!strcmp(stage, "cleared")) {
        /* the Hub's files one by one, so the screen can count them; the tree below takes the rest */
        count_files(progress, rec);
        for (int i = 0; i < rec->n_files; i++) {
            char path[PATH_MAX_LEN * 2];
            snprintf(path, sizeof(path), "%s/%s", p->gone, rec->files[i].path);
            fs_remove(path);
            if (progress)
                progress->done = (uint32_t)i + 1;
        }
        if (fs_remove_tree(p->gone))
            return PATCH_WRITE;
        if (fs_is_dir(p->backup)) {
            Restore r = {p, 0};
            fs_walk(p->backup, restore_one, &r);
            if (r.failed)
                return PATCH_WRITE;
        }
        if (write_pending(p, "remove", "restored", rec))
            return PATCH_WRITE;
    }
    if (fs_remove_tree(p->backup) || record_delete(root, rec->slug))
        return PATCH_WRITE;
    return fs_remove(p->pending) ? PATCH_WRITE : PATCH_OK;
}

PatchResult patch_remove(const char *root, const Record *rec, PatchProgress *progress)
{
    Paths p;
    paths(&p, root, rec);
    if (write_pending(&p, "remove", "start", rec))
        return PATCH_WRITE;
    return finish_remove(root, &p, rec, "start", progress);
}

/* ---------------------------------------------------------------- recovery */

RecoverResult patch_recover(const char *root, char *slug, size_t slug_size)
{
    if (slug && slug_size)
        slug[0] = 0;
    char pending[PATH_MAX_LEN];
    snprintf(pending, sizeof(pending), "%s" HUB_SUBDIR "/pending.txt", root);
    fs_finish_replace(pending);
    FILE *f = fopen(pending, "rb");
    if (!f)
        return RECOVER_NONE;
    char *text = malloc(1024 * 1024);
    size_t got = text ? fread(text, 1, 1024 * 1024 - 1, f) : 0;
    fclose(f);
    if (!text)
        return RECOVER_NONE;
    text[got] = 0;
    char op[16] = "", stage[16] = "";
    sscanf(text, "op=%15s\nstage=%15s", op, stage);
    const char *body = strstr(text, "slug=");
    Record rec;
    if (!body || record_parse(&rec, body)) {
        free(text);
        fs_remove(pending); /* unreadable: nothing can be finished from it */
        return RECOVER_CLEARED;
    }
    if (slug && slug_size)
        snprintf(slug, slug_size, "%s", rec.slug);
    Paths p;
    paths(&p, root, &rec);
    RecoverResult result;

    bool swapped = !strcmp(stage, "swapped") || (!strcmp(stage, "swapping") && !fs_is_dir(p.fresh));
    if (!strcmp(op, "install") && swapped) {
        /* the new set is live (or the second rename was the last thing done): finish forward */
        if (fs_remove_tree(p.old) == 0 && record_save(root, &rec) == 0)
            fs_remove(pending);
        result = RECOVER_INSTALLED;
    } else if (!strcmp(op, "install")) {
        /* cut before the swap, or between its renames: back to the old set */
        if (!strcmp(stage, "swapping") && !fs_is_dir(p.live) && fs_is_dir(p.old))
            fs_rename(p.old, p.live);
        if (fs_remove_tree(p.fresh) == 0 && !fs_is_dir(p.old))
            fs_remove(pending);
        result = RECOVER_ROLLED_BACK;
    } else if (!strcmp(op, "remove")) {
        finish_remove(root, &p, &rec, stage, NULL);
        result = RECOVER_REMOVED;
    } else {
        fs_remove(pending);
        result = RECOVER_CLEARED;
    }
    record_free(&rec);
    free(text);
    return result;
}

/* ---------------------------------------------------------------- the check before installing */

int patch_check_game(const char *zip_path, const char *game_root, int *matched, int *total)
{
    *matched = *total = 0;
    Zip zip;
    if (zip_open(&zip, zip_path))
        return -1;
    int i = zip_find(&zip, "check.txt");
    char *text = i >= 0 ? zip_read(&zip, i, NULL) : NULL;
    zip_close(&zip);
    if (!text)
        return -1;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char want[65], rel[256];
        if (sscanf(line, "%64s %255[^\n]", want, rel) != 2)
            continue;
        const char *path = rel;
        while (*path == ' ' || *path == '*')
            path++; /* sha256sum writes "  " or " *" */
        if (!strncmp(path, "romfs/", 6))
            path += 6;
        char full[PATH_MAX_LEN * 2];
        snprintf(full, sizeof(full), "%s/%s", game_root, path);
        uint8_t sha[32];
        char hex[65];
        (*total)++;
        if (fs_sha256(full, sha))
            continue;
        for (int b = 0; b < 32; b++)
            snprintf(hex + 2 * b, 3, "%02x", sha[b]);
        if (!strcmp(hex, want))
            (*matched)++;
    }
    free(text);
    return 0;
}
