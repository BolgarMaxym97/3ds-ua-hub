/* Host test of the QR codes J8 draws (vendor/qrcodegen.c): every address the catalog allows fits,
 * small enough to draw on the top screen. */
#include <stdio.h>
#include <string.h>

#include "../vendor/qrcodegen.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

/* Encoded the way qr_screen.c does it: ECC M, the version as small as it goes. */
static int size_of(const char *text)
{
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return 0;
    return qrcodegen_getSize(qr);
}

int main(void)
{
    expect(size_of("https://storage.example/games/0004000000XXXX00") == 33, "a short address: version 4, 33 modules");
    char longest[201];
    memset(longest, 'a', 200);
    memcpy(longest, "https://", 8);
    longest[200] = 0;
    int n = size_of(longest);
    /* the top screen holds 200 px for it: 2 px a module with the quiet zone up to 92 modules */
    expect(n > 0 && n + 8 <= 100, "the longest address the catalog allows (200): fits at 2 px a module");
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
