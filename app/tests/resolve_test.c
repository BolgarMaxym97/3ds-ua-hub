/* Host test of source/resolve.c on Super Mario 3D Land's two variants (EUR, USA). */
#include <stdio.h>
#include <string.h>

#include "../source/resolve.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static CatItem mario(int usa_needs_update)
{
    CatItem it;
    memset(&it, 0, sizeof it);
    strcpy(it.slug, "super-mario-3d-land");
    it.kind = KIND_PATCH;
    it.n_variants = 2;
    it.variants[0] = (CatVariant){.region = "EUR", .title_id = 0x0004000000053F00ULL, .product = "CTR-P-AREP",
                                  .game_version = -1, .size = 68050, .file = {.present = true}};
    it.variants[1] = (CatVariant){.region = "USA", .title_id = 0x0004000000054000ULL, .product = "CTR-P-AREE",
                                  .game_version = usa_needs_update, .size = 68050, .file = {.present = true}};
    return it;
}

static ConsoleTitle title(uint64_t tid, const char *product, int update)
{
    ConsoleTitle t;
    memset(&t, 0, sizeof t);
    t.title_id = tid;
    strcpy(t.product, product);
    t.update_version = update;
    return t;
}

/* the title AM reports as installed, and its version */
static uint64_t installed_tid;
static int installed_at;

static int fake_installed(uint64_t tid)
{
    return tid == installed_tid ? installed_at : -1;
}

int main(void)
{
    CatItem m = mario(-1);
    Match match;
    ConsoleTitle eur = title(0x0004000000053F00ULL, "CTR-P-AREP", UPDATE_NONE);
    ConsoleTitle usa = title(0x0004000000054000ULL, "CTR-P-AREE", UPDATE_NONE);
    ConsoleTitle kart = title(0x0004000000030700ULL, "CTR-P-AMKP", UPDATE_NONE);

    ConsoleTitle only_eur[] = {kart, eur};
    expect(resolve_patch(&m, only_eur, 2, "EUR", &match) == MATCH_FOUND && match.variant == 0 && match.title == 1,
           "EUR game: the EUR variant, found among other games");

    ConsoleTitle only_usa[] = {usa};
    expect(resolve_patch(&m, only_usa, 1, "EUR", &match) == MATCH_FOUND && match.variant == 1,
           "USA game on a EUR console: the USA variant (the game decides, not the console)");

    ConsoleTitle both[] = {usa, eur};
    expect(resolve_patch(&m, both, 2, "EUR", &match) == MATCH_FOUND && match.variant == 0 && match.title == 1,
           "both games: the console's region wins");
    expect(resolve_patch(&m, both, 2, "JPN", &match) == MATCH_SEVERAL && match.n_candidates == 2,
           "both games, neither the console's region: ask");

    ConsoleTitle none[] = {kart};
    expect(resolve_patch(&m, none, 1, "EUR", &match) == MATCH_NOT_FOUND, "no game of it: not found");
    expect(resolve_patch(&m, none, 0, "EUR", &match) == MATCH_NOT_FOUND, "no games at all: not found");

    CatItem needs = mario(1040); /* the USA patch was made for update v1.1.0 */
    ConsoleTitle usa_old = title(0x0004000000054000ULL, "CTR-P-AREE", UPDATE_NONE);
    ConsoleTitle usa_new = title(0x0004000000054000ULL, "CTR-P-AREE", 1040);
    expect(resolve_patch(&needs, &usa_old, 1, "USA", &match) == MATCH_WRONG_VERSION && match.variant == 1,
           "needs update 1040, none installed: wrong version");
    expect(resolve_patch(&needs, &usa_new, 1, "USA", &match) == MATCH_FOUND, "needs update 1040, 1040 installed: found");
    expect(version_fits(&needs.variants[0], 2064), "any version: any update fits");
    CatVariant base = needs.variants[1];
    base.game_version = 0;
    expect(version_fits(&base, UPDATE_NONE) && !version_fits(&base, 1040), "version 0: only the game without updates");

    /* picked by hand: a copy under another Title ID */
    ConsoleTitle copy = title(0x0004000000F00D00ULL, "CTR-P-AREP", UPDATE_NONE);
    expect(resolve_manual(&m, &copy) == 0, "by hand: the EUR product code picks the EUR variant");
    ConsoleTitle jpn = title(0x0004000000F00D00ULL, "CTR-P-AREJ", UPDATE_NONE);
    expect(resolve_manual(&m, &jpn) == -1, "by hand: a JPN game has no variant (GAME_REGION)");
    ConsoleTitle unknown = title(0x0004000000F00D00ULL, "", UPDATE_NONE);
    expect(resolve_manual(&m, &unknown) == -1, "by hand: no product code and two variants: no guess");
    CatItem single = m;
    single.n_variants = 1;
    expect(resolve_manual(&single, &unknown) == 0, "by hand: one variant is offered, the file check decides");

    CatItem cia = m;
    cia.kind = KIND_CIA;
    expect(resolve_cia(&cia, "USA") == 1 && resolve_cia(&cia, "KOR") == 0, "CIA: the console's region, else the first");

    /* a CIA put on the console without the Hub */
    expect(cia_title_version("2.1.0") == 2 * 1024 + 1 * 16 && cia_title_version("1.0") == 1024 &&
               cia_title_version("3") == 3 * 1024,
           "CIA version: major.minor.micro as AM packs it");
    expect(cia_title_version("1.64.0") == -1 && cia_title_version("1.0.16") == -1 && cia_title_version("v1") == -1 &&
               cia_title_version("1.0.0.1") == -1 && cia_title_version("") == -1,
           "CIA version: what AM cannot hold is no version");
    CatItem ua = cia;
    strcpy(ua.version, "2.1.0");
    installed_tid = 0x0004000000054000ULL; /* the USA variant's title */
    installed_at = 2 * 1024 + 16;
    expect(resolve_installed_cia(&ua, fake_installed) == 1, "installed CIA: found at the catalog's version");
    installed_at = 2 * 1024;
    expect(resolve_installed_cia(&ua, fake_installed) == -1, "installed CIA: another version (an original game) is not it");
    installed_at = 2 * 1024 + 16;
    ua.variants[1].data.present = true;
    expect(resolve_installed_cia(&ua, fake_installed) == -1, "installed CIA: one that needs SD data is not adopted");
    expect(resolve_installed_cia(&m, fake_installed) == -1, "installed CIA: a patch never");

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
