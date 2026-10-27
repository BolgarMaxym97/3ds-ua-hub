#include "resolve.h"

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

int resolve_cia(const CatItem *item, const char *console_region)
{
    for (int v = 0; v < item->n_variants; v++)
        if (console_region && !strcmp(item->variants[v].region, console_region))
            return v;
    return item->n_variants ? 0 : -1;
}
