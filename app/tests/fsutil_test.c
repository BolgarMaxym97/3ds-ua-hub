/* Host test of source/core/fsutil.c: the replace-in-two-steps the catalog cache relies on. */
#include <stdio.h>
#include <string.h>

#include "../source/core/fsutil.h"
#include "../source/core/sha256.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static int content_is(const char *path, const char *want)
{
    char buf[64] = {0};
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    return n == strlen(want) && !memcmp(buf, want, n);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/fsutil_sd";
    char path[256], ok[256];
    fs_remove_tree(dir);
    snprintf(path, sizeof path, "%s/cache/catalog.json", dir);
    snprintf(ok, sizeof ok, "%s" FS_OK_SUFFIX, path);
    uint8_t sha[32];

    sha256("v1", 2, sha);
    expect(fs_replace_verified(path, "v1", 2, sha) == 0 && content_is(path, "v1"), "replace: first write");
    expect(!fs_exists(ok), "replace: nothing left pending");

    /* the bug of 2026-10-25: reading the cache called this with nothing pending, and it deleted the file */
    expect(fs_finish_replace(path) == 0 && content_is(path, "v1"), "finish with nothing pending keeps the file");

    sha256("v2", 2, sha);
    expect(fs_replace_verified(path, "v2", 2, sha) == 0 && content_is(path, "v2"), "replace: second write");

    /* a power cut after the new copy was checked: the .hub-ok is completed on the next start */
    fs_write(ok, "v3", 2);
    expect(fs_finish_replace(path) == 0 && content_is(path, "v3") && !fs_exists(ok), "finish completes a pending copy");

    /* a cut before that: a partial .hub-tmp never replaces anything */
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s" FS_TMP_SUFFIX, path);
    fs_write(tmp, "v", 1);
    expect(fs_finish_replace(path) == 0 && content_is(path, "v3"), "a partial temp copy is ignored");

    fs_remove_tree(dir);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
