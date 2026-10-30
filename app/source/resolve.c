#include "resolve.h"

#include <stdlib.h>
#include <string.h>

bool version_fits(const CatVariant *v, int update_version)
{
    if (v->game_version < 0)
        return true; /* any version */
    /* 0 means "the game without updates" */
    int have = update_version == UPDATE_NONE ? 0 : update_version;
    return have == v->game_version;
}

static int find_title(const ConsoleTitle *titles, int n, uint64_t tid)
{
    for (int i = 0; i < n; i++)
        if (titles[i].title_id == tid)
            return i;
    return -1;
}

MatchResult resolve_patch(const CatItem *item, const ConsoleTitle *titles, int n, const char *console_region, Match *out)
{
    memset(out, 0, sizeof *out);
    out->variant = out->title = -1;
    for (int v = 0; v < item->n_variants; v++)
        if (find_title(titles, n, item->variants[v].title_id) >= 0)
            out->candidates[out->n_candidates++] = v;
    if (!out->n_candidates)
        return MATCH_NOT_FOUND;

    int pick = -1;
    if (out->n_candidates == 1) {
        pick = out->candidates[0];
    } else {
        for (int i = 0; i < out->n_candidates && pick < 0; i++)
            if (console_region && !strcmp(item->variants[out->candidates[i]].region, console_region))
                pick = out->candidates[i];
        if (pick < 0)
            return MATCH_SEVERAL;
    }
    out->variant = pick;
    out->title = find_title(titles, n, item->variants[pick].title_id);
    return version_fits(&item->variants[pick], titles[out->title].update_version) ? MATCH_FOUND : MATCH_WRONG_VERSION;
}

int resolve_manual(const CatItem *item, const ConsoleTitle *title)
{
    const char *region = region_of_product(title->product);
    for (int v = 0; v < item->n_variants && region; v++)
        if (!strcmp(item->variants[v].region, region))
            return v;
    /* a translation with only one variant: offer it, the file check will tell */
    return item->n_variants == 1 ? 0 : -1;
}

/* The game in a product code: "CTR-P-AREP" -> "ARE" (`len` 3), NULL if it has no such part. */
static const char *game_code(const char *product, size_t *len)
{
    const char *dash = strrchr(product, '-');
    const char *code = dash ? dash + 1 : product;
    size_t n = strlen(code);
    if (n < 2)
        return NULL;
    *len = n - 1; /* the last letter is the region */
    return code;
}

bool resolve_other_game(const CatItem *item, const ConsoleTitle *title)
{
    size_t n, vn;
    const char *code = game_code(title->product, &n);
    if (!code)
        return false;
    bool known = false;
    for (int v = 0; v < item->n_variants; v++) {
        const char *vcode = game_code(item->variants[v].product, &vn);
        if (!vcode)
            continue;
        known = true;
        if (vn == n && !strncmp(vcode, code, n))
            return false;
    }
    return known;
}

int resolve_cia(const CatItem *item, const char *console_region)
{
    for (int v = 0; v < item->n_variants; v++)
        if (console_region && !strcmp(item->variants[v].region, console_region))
            return v;
    return item->n_variants ? 0 : -1;
}

int cia_title_version(const char *version)
{
    long part[3] = {0, 0, 0};
    const char *p = version;
    for (int i = 0; i < 3; i++) {
        char *end;
        part[i] = strtol(p, &end, 10);
        if (end == p || part[i] < 0)
            return -1;
        p = end;
        if (!*p)
            break;
        if (*p != '.' || i == 2)
            return -1;
        p++;
    }
    if (part[0] > 63 || part[1] > 63 || part[2] > 15)
        return -1;
    return (int)(part[0] << 10 | part[1] << 4 | part[2]);
}

int resolve_installed_cia(const CatItem *item, int (*installed_version)(uint64_t title_id))
{
    int want = cia_title_version(item->version);
    if (item->kind != KIND_CIA || want < 0)
        return -1;
    for (int v = 0; v < item->n_variants; v++)
        if (!item->variants[v].data.present && installed_version(item->variants[v].title_id) == want)
            return v;
    return -1;
}
