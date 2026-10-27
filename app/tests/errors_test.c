/* Host test of source/errors.c: every code has a complete entry, at most one %s, unique codes. */
#include <stdio.h>
#include <string.h>

#include "../source/errors.h"

int main(void)
{
    int failures = 0;
    for (int i = 1; i < ERR_COUNT; i++) {
        const ErrorInfo *e = error_info((ErrorCode)i);
        const char *pct = strstr(e->text, "%");
        int ok = e->code && e->title && e->text && e->title[0] && e->text[0] &&
                 (!pct || (pct[1] == 's' && !strchr(pct + 1, '%')));
        for (int j = 1; j < i; j++)
            if (!strcmp(error_info((ErrorCode)j)->code, e->code))
                ok = 0;
        printf("%s %s\n", ok ? "ok  " : "FAIL", e->code);
        failures += !ok;
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
