/* Host test of source/catalog.c: signature, parsing, versions. Run from the repository root. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/catalog.h"
#include "../source/catalog_key.h"

#define FIX "app/tests/fixtures/"

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        exit(2);
    buf[n] = 0;
    fclose(f);
    if (len)
        *len = (size_t)n;
    return buf;
}

static void test_key(uint8_t key[32])
{
    char *hex = slurp(FIX "public_key.hex", NULL);
    for (int i = 0; i < 32; i++)
        sscanf(hex + 2 * i, "%2hhx", &key[i]);
    free(hex);
}

int main(void)
{
    uint8_t key[32];
    test_key(key);
    size_t len;
    char *body = slurp(FIX "catalog.json", &len);
    char *sig = slurp(FIX "catalog.sig", NULL);

    /* signature */
    expect(catalog_verify((uint8_t *)body, len, sig, key), "signature: good body passes");
    body[20] ^= 1;
    expect(!catalog_verify((uint8_t *)body, len, sig, key), "signature: one flipped bit fails");
    body[20] ^= 1;
    expect(!catalog_verify((uint8_t *)body, len, "abc", key), "signature: a short header fails");
    expect(!catalog_verify((uint8_t *)body, len, sig, CATALOG_PUBLIC_KEY), "signature: another key fails");

    /* the live Worker, signed by Cloudflare's WebCrypto, checked by Monocypher with the app's key */
    size_t live_len;
    char *live = slurp(FIX "live_empty.json", &live_len);
    char *live_sig = slurp(FIX "live_empty.sig", NULL);
    Catalog cat;
    expect(catalog_load(&cat, live, live_len, live_sig, CATALOG_PUBLIC_KEY) == CAT_OK && cat.n == 0,
           "live: the deployed Worker's signature checks with the built-in key");
    catalog_free(&cat);

    /* parsing */
    expect(catalog_load(&cat, body, len, sig, key) == CAT_OK, "load: ok");
    expect(cat.n == 2, "load: entries the app cannot use are left out (bad slug, no variants, bad kind)");
    CatItem *mario = &cat.items[0];
    expect(!strcmp(mario->slug, "super-mario-3d-land") && mario->kind == KIND_PATCH, "mario: slug and kind");
    expect(mario->n_variants == 2, "mario: two variants");
    expect(mario->variants[0].title_id == 0x0004000000053F00ULL && !strcmp(mario->variants[0].region, "EUR"), "mario: EUR title id");
    expect(mario->variants[1].title_id == 0x0004000000054000ULL && mario->variants[1].game_version == 1040, "mario: USA needs update 1040");
    expect(mario->variants[0].game_version == -1, "mario: EUR takes any version");
    expect(mario->variants[0].size == 68050 && mario->variants[0].file.present, "mario: file size and key");
    expect(mario->text_pct == 100 && mario->textures_pct == 100, "mario: progress");
    expect(mario->banner.present && !strcmp(mario->banner.key, "super-mario-3d-land/banner.png"), "mario: banner");
    expect(mario->description && strstr(mario->description, "і, ї, є"), "mario: Ukrainian description intact");
    expect(mario->installs == 3, "mario: installs");

    CatItem *cia = &cat.items[1];
    expect(cia->kind == KIND_CIA && cia->third_party, "cia: kind, third party");
    expect(cia->text_pct == 100 && cia->textures_pct == -1, "cia: unknown textures stay unknown");
    expect(!cia->banner.present && !cia->icon.present && !cia->description, "cia: no banner, icon, description");
    expect(strlen(cia->title) < sizeof cia->title, "cia: a long title is cut to fit");
    {
        /* cut at a letter boundary: the last byte starts or completes a UTF-8 letter */
        size_t n = strlen(cia->title);
        unsigned char last = (unsigned char)cia->title[n - 1];
        int lead = n >= 2 ? (unsigned char)cia->title[n - 2] : 0;
        expect(last < 0x80 || ((last & 0xC0) == 0x80 && (lead & 0xE0) == 0xC0), "cia: the cut leaves no half letter");
    }
    expect(!strcmp(cia->min_app, "9.0.0"), "cia: needs a newer app");
    catalog_free(&cat);

    size_t v2_len;
    char *v2 = slurp(FIX "catalog_v2.json", &v2_len);
    char *v2_sig = slurp(FIX "catalog_v2.sig", NULL);
    expect(catalog_load(&cat, v2, v2_len, v2_sig, key) == CAT_TOO_NEW, "load: a newer catalog shape asks for an app update");
    expect(catalog_load(&cat, body, len, v2_sig, key) == CAT_BAD_SIGNATURE, "load: a mismatched signature is refused");

    /* a newer catalog that names the app version it needs (A4: "Потрібна v1.1.0") */
    size_t app_len;
    uint8_t app_key[32];
    char *app_body = slurp(FIX "catalog_v2_app.json", &app_len);
    char *app_sig = slurp(FIX "catalog_v2_app.sig", NULL);
    char *app_hex = slurp(FIX "catalog_v2_app.key", NULL);
    for (int i = 0; i < 32; i++)
        sscanf(app_hex + 2 * i, "%2hhx", &app_key[i]);
    expect(catalog_load(&cat, app_body, app_len, app_sig, app_key) == CAT_TOO_NEW, "load: v2 with an app version is too new");
    expect(!strcmp(cat.need_app, "1.1.0"), "load: the app version it needs is kept");
    expect(catalog_load(&cat, v2, v2_len, v2_sig, key) == CAT_TOO_NEW && !cat.need_app[0],
           "load: v2 without one leaves it empty");
    free(app_body);
    free(app_sig);
    free(app_hex);

    /* a CIA that keeps its game on the SD card (Undertale's port): the data and where it goes */
    size_t data_len;
    uint8_t data_key[32];
    char *data_body = slurp(FIX "catalog_data.json", &data_len);
    char *data_sig = slurp(FIX "catalog_data.sig", NULL);
    char *data_hex = slurp(FIX "catalog_data.key", NULL);
    for (int i = 0; i < 32; i++)
        sscanf(data_hex + 2 * i, "%2hhx", &data_key[i]);
    expect(catalog_load(&cat, data_body, data_len, data_sig, data_key) == CAT_OK, "data: loads");
    expect(cat.n == 1 && !strcmp(cat.items[0].slug, "undertale"),
           "data: a folder outside 3ds/, the Hub's own folder, a missing size: left out");
    const CatVariant *ut = &cat.items[0].variants[0];
    expect(ut->data.present && !strcmp(ut->data.key, "undertale/data/1.0/eur.zip") && ut->data_size == 250000000,
           "data: key and size");
    expect(!strcmp(ut->data_dir, "3ds/butterscotch/undertale"), "data: folder");
    catalog_free(&cat);
    expect(catalog_load(&cat, body, len, sig, key) == CAT_OK && !cat.items[0].variants[0].data.present,
           "data: a patch has none");
    catalog_free(&cat);
    free(data_body);
    free(data_sig);
    free(data_hex);

    /* a translation kept at the top of the list (the system one) */
    size_t pin_len;
    uint8_t pin_key[32];
    char *pin_body = slurp(FIX "catalog_pinned.json", &pin_len);
    char *pin_sig = slurp(FIX "catalog_pinned.sig", NULL);
    char *pin_hex = slurp(FIX "catalog_pinned.key", NULL);
    for (int i = 0; i < 32; i++)
        sscanf(pin_hex + 2 * i, "%2hhx", &pin_key[i]);
    expect(catalog_load(&cat, pin_body, pin_len, pin_sig, pin_key) == CAT_OK && cat.n == 2, "pinned: loads");
    expect(cat.items[0].pinned && !cat.items[1].pinned, "pinned: true when set, false when the field is missing");
    catalog_free(&cat);
    free(pin_body);
    free(pin_sig);
    free(pin_hex);

    /* screenshots: the broken ones left out, the rest in order, at most six */
    size_t shot_len;
    uint8_t shot_key[32];
    char *shot_body = slurp(FIX "catalog_shots.json", &shot_len);
    char *shot_sig = slurp(FIX "catalog_shots.sig", NULL);
    char *shot_hex = slurp(FIX "catalog_shots.key", NULL);
    for (int i = 0; i < 32; i++)
        sscanf(shot_hex + 2 * i, "%2hhx", &shot_key[i]);
    expect(catalog_load(&cat, shot_body, shot_len, shot_sig, shot_key) == CAT_OK && cat.n == 2, "shots: loads");
    const CatItem *sm = &cat.items[0];
    expect(sm->n_shots == CAT_MAX_SHOTS, "shots: a bad hash, a null and an unsafe key left out, the eighth past six");
    expect(!strcmp(sm->shots[0].file.key, "super-mario-3d-land/shots/1.png") &&
               !strcmp(sm->shots[1].file.key, "super-mario-3d-land/shots/3.png") &&
               !strcmp(sm->shots[5].file.key, "super-mario-3d-land/shots/7.png"),
           "shots: in the catalog's order");
    expect(sm->shots[0].caption && !strcmp(sm->shots[0].caption, "Вибір файлу: «НОВА ГРА»"), "shots: caption");
    expect(!sm->shots[1].caption && !sm->shots[2].caption, "shots: null or missing caption: none");
    expect(cat.items[1].n_shots == 0, "shots: none when the field is missing");
    catalog_free(&cat);
    free(shot_body);
    free(shot_sig);
    free(shot_hex);

    /* a patch's game from another server: size and sha256; a broken one loses only the offer */
    size_t fb_len;
    uint8_t fb_key[32];
    char *fb_body = slurp(FIX "catalog_fallback.json", &fb_len);
    char *fb_sig = slurp(FIX "catalog_fallback.sig", NULL);
    char *fb_hex = slurp(FIX "catalog_fallback.key", NULL);
    for (int i = 0; i < 32; i++)
        sscanf(fb_hex + 2 * i, "%2hhx", &fb_key[i]);
    expect(catalog_load(&cat, fb_body, fb_len, fb_sig, fb_key) == CAT_OK && cat.n == 2, "fallback: loads");
    const CatItem *fm = &cat.items[0];
    expect(fm->n_variants == 3, "fallback: a broken one keeps its variant");
    expect(fm->variants[0].fallback && fm->variants[0].fallback_size == 536870912 &&
               !strcmp(fm->variants[0].fallback_sha256,
                       "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
           "fallback: size and sha256");
    expect(!fm->variants[1].fallback && !fm->variants[2].fallback, "fallback: a bad hash or no size: none");
    expect(!cat.items[1].variants[0].fallback, "fallback: never for a CIA translation");
    expect(!strcmp(fm->variants[1].qr_url, "https://games.example/p/mario-usa") && !fm->variants[0].qr_url[0],
           "qr: the game's page, none when the field is missing");
    expect(!fm->variants[2].qr_url[0], "qr: not a web address: left out");
    expect(!cat.items[1].variants[0].qr_url[0], "qr: never for a CIA translation");
    catalog_free(&cat);
    expect(catalog_load(&cat, body, len, sig, key) == CAT_OK && !cat.items[0].variants[0].fallback,
           "fallback: none when the field is missing");
    catalog_free(&cat);
    free(fb_body);
    free(fb_sig);
    free(fb_hex);

    /* versions and regions */
    expect(version_cmp("1.10.0", "1.9.2") > 0, "version: 1.10.0 > 1.9.2");
    expect(version_cmp("1.0", "1.0.0") == 0, "version: 1.0 == 1.0.0");
    expect(version_cmp("0.1.0", "9.0.0") < 0, "version: 0.1.0 < 9.0.0");
    expect(!strcmp(region_of_product("CTR-P-AREP"), "EUR") && !strcmp(region_of_product("CTR-P-AREE"), "USA"),
           "region: from the product code");
    expect(region_of_product("") == NULL && region_of_product(NULL) == NULL, "region: unknown");

    free(body);
    free(sig);
    free(live);
    free(live_sig);
    free(v2);
    free(v2_sig);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
