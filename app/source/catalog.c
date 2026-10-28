#include "catalog.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/cJSON.h"
#include "../vendor/monocypher-ed25519.h"

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool catalog_verify(const uint8_t *body, size_t len, const char *sig_hex, const uint8_t public_key[32])
{
    uint8_t sig[64];
    if (!sig_hex || strlen(sig_hex) != 128)
        return false;
    for (int i = 0; i < 64; i++) {
        int hi = hex_digit(sig_hex[2 * i]), lo = hex_digit(sig_hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        sig[i] = (uint8_t)(hi << 4 | lo);
    }
    return crypto_ed25519_check(sig, public_key, body, len) == 0;
}

uint64_t variant_bytes(const CatVariant *v)
{
    return (uint64_t)v->size + (v->data.present ? v->data_size : 0);
}

int version_cmp(const char *a, const char *b)
{
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == '.')
            a++;
        if (*b == '.')
            b++;
        if ((*a && !isdigit((unsigned char)*a)) || (*b && !isdigit((unsigned char)*b)))
            break;
    }
    return 0;
}

const char *region_of_product(const char *product)
{
    size_t n = product ? strlen(product) : 0;
    if (n < 4)
        return NULL;
    switch (product[n - 1]) {
    case 'P': return "EUR";
    case 'E': return "USA";
    case 'J': return "JPN";
    case 'K': return "KOR";
    case 'C': return "CHN";
    case 'W': return "TWN";
    default: return NULL;
    }
}

/* ---------------------------------------------------------------- parsing */

/* The text of `key`, or "" when it is absent or null. Too long: cut, never overrun. */
static void copy_text(char *dst, size_t size, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    const char *s = cJSON_IsString(v) ? v->valuestring : "";
    size_t n = strlen(s);
    if (n >= size) {
        n = size - 1;
        /* cut at the start of a letter, not inside one: a half UTF-8 letter shows as garbage */
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(dst, s, n);
    dst[n] = 0;
}

static char *dup_text(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v))
        return NULL;
    char *s = malloc(strlen(v->valuestring) + 1);
    if (s)
        strcpy(s, v->valuestring);
    return s;
}

/* A number 0..100, or -1 for null (unknown). */
static int percent(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 100)
        return -1;
    return (int)v->valuedouble;
}

static bool is_sha256(const char *s)
{
    if (strlen(s) != 64)
        return false;
    for (int i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return true;
}

/* Keys become file names on the SD card too: only the shapes the Worker serves. */
static bool is_safe_key(const char *s)
{
    if (!*s || strstr(s, "..") || s[0] == '/')
        return false;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '-' || *s == '.' || *s == '/'))
            return false;
    return true;
}

static bool parse_file(CatFile *f, const cJSON *obj)
{
    memset(f, 0, sizeof *f);
    if (!cJSON_IsObject(obj))
        return true; /* null: no such file */
    copy_text(f->key, sizeof f->key, obj, "key");
    copy_text(f->sha256, sizeof f->sha256, obj, "sha256");
    f->present = is_safe_key(f->key) && is_sha256(f->sha256);
    return f->present;
}

/* Where a CIA's data may go: a folder under 3ds/, never the Hub's own. */
static bool is_data_dir(const char *s)
{
    if (strncmp(s, "3ds/", 4) || !s[4] || !strncmp(s, "3ds/3ds-ua-hub", 14) || strstr(s, "..") || strstr(s, "//") ||
        s[strlen(s) - 1] == '/')
        return false;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '-' || *s == '.' || *s == '_' || *s == '/'))
            return false;
    return true;
}

static bool parse_data(CatVariant *v, const cJSON *obj)
{
    if (!obj)
        return true; /* most variants have none */
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(obj, "size");
    if (!parse_file(&v->data, obj) || !v->data.present || !cJSON_IsNumber(size) || size->valuedouble <= 0 ||
        size->valuedouble > 4e9)
        return false;
    v->data_size = (uint32_t)size->valuedouble;
    copy_text(v->data_dir, sizeof v->data_dir, obj, "dir");
    return is_data_dir(v->data_dir);
}

static bool parse_variant(CatVariant *v, const cJSON *obj)
{
    memset(v, 0, sizeof *v);
    char tid[17];
    copy_text(v->region, sizeof v->region, obj, "region");
    copy_text(tid, sizeof tid, obj, "tid");
    copy_text(v->product, sizeof v->product, obj, "product");
    if (strlen(tid) != 16)
        return false;
    char *end;
    v->title_id = strtoull(tid, &end, 16);
    if (*end)
        return false;
    const cJSON *gv = cJSON_GetObjectItemCaseSensitive(obj, "game_version");
    v->game_version = cJSON_IsNumber(gv) ? (int)gv->valuedouble : -1;
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(obj, "size");
    if (!cJSON_IsNumber(size) || size->valuedouble <= 0 || size->valuedouble > 4e9)
        return false;
    v->size = (uint32_t)size->valuedouble;
    copy_text(v->file.key, sizeof v->file.key, obj, "key");
    copy_text(v->file.sha256, sizeof v->file.sha256, obj, "sha256");
    v->file.present = is_safe_key(v->file.key) && is_sha256(v->file.sha256);
    return v->file.present && strlen(v->region) == 3 && parse_data(v, cJSON_GetObjectItemCaseSensitive(obj, "data"));
}

static bool parse_item(CatItem *it, const cJSON *obj)
{
    memset(it, 0, sizeof *it);
    copy_text(it->slug, sizeof it->slug, obj, "slug");
    copy_text(it->title, sizeof it->title, obj, "title");
    copy_text(it->version, sizeof it->version, obj, "version");
    copy_text(it->released, sizeof it->released, obj, "released");
    copy_text(it->authors, sizeof it->authors, obj, "authors");
    copy_text(it->min_app, sizeof it->min_app, obj, "min_app");
    char kind[8];
    copy_text(kind, sizeof kind, obj, "kind");
    if (!strcmp(kind, "patch"))
        it->kind = KIND_PATCH;
    else if (!strcmp(kind, "cia"))
        it->kind = KIND_CIA;
    else
        return false;
    it->text_pct = percent(obj, "text");
    it->textures_pct = percent(obj, "textures");
    it->third_party = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, "third_party"));
    it->pinned = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, "pinned"));
    const cJSON *installs = cJSON_GetObjectItemCaseSensitive(obj, "installs");
    it->installs = cJSON_IsNumber(installs) ? (int)installs->valuedouble : 0;
    /* A broken banner or icon only loses the picture, not the translation. */
    parse_file(&it->banner, cJSON_GetObjectItemCaseSensitive(obj, "banner"));
    parse_file(&it->icon, cJSON_GetObjectItemCaseSensitive(obj, "icon"));

    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(obj, "variants"))
    {
        if (it->n_variants == CAT_MAX_VARIANTS)
            break;
        if (parse_variant(&it->variants[it->n_variants], v))
            it->n_variants++;
    }
    if (!is_safe_key(it->slug) || strchr(it->slug, '/') || !it->title[0] || !it->n_variants)
        return false;
    it->description = dup_text(obj, "description");
    it->changelog = dup_text(obj, "changelog");
    return true;
}

CatResult catalog_load(Catalog *out, const char *body, size_t len, const char *sig_hex, const uint8_t public_key[32])
{
    memset(out, 0, sizeof *out);
    if (!catalog_verify((const uint8_t *)body, len, sig_hex, public_key))
        return CAT_BAD_SIGNATURE;
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (!root)
        return CAT_BAD_JSON;
    CatResult result = CAT_OK;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsNumber(v) || !cJSON_IsArray(items)) {
        result = CAT_BAD_JSON;
    } else if ((int)v->valuedouble > CATALOG_VERSION) {
        copy_text(out->need_app, sizeof out->need_app, root, "app"); /* A4: "Потрібна v1.1.0" */
        result = CAT_TOO_NEW;
    } else {
        int n = cJSON_GetArraySize(items);
        out->items = calloc(n ? (size_t)n : 1, sizeof *out->items);
        if (!out->items) {
            result = CAT_NO_MEMORY;
        } else {
            const cJSON *item;
            /* an entry this app cannot use is left out, the rest still shows */
            cJSON_ArrayForEach(item, items) if (parse_item(&out->items[out->n], item)) out->n++;
        }
    }
    cJSON_Delete(root);
    return result;
}

void catalog_free(Catalog *cat)
{
    for (int i = 0; i < cat->n; i++) {
        free(cat->items[i].description);
        free(cat->items[i].changelog);
    }
    free(cat->items);
    memset(cat, 0, sizeof *cat);
}
