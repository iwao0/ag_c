// Wasm standalone stdio.h vsnprintf/vsprintf stubs
// Expected: exit=0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int fmt_vsn(char *buf, unsigned long size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static int fmt_vs(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int main(void) {
    char buf[32];
    int n = fmt_vsn(buf, sizeof(buf), "%d-%u-%s-%c-%%", -12, 34u, "ok", 65);
    if (n != 13) return 1;
    if (strcmp(buf, "-12-34-ok-A-%") != 0) return 2;

    char padded[16];
    int p = fmt_vsn(padded, sizeof(padded), "[%02d]", 7);
    if (p != 4) return 3;
    if (strcmp(padded, "[07]") != 0) return 4;

    char small[4];
    int t = fmt_vsn(small, sizeof(small), "%d", 12345);
    if (t != 5) return 5;
    if (strcmp(small, "123") != 0) return 6;

    char count_only[2] = {'q', 'r'};
    int z = fmt_vsn(count_only, 0, "%d-%d", 5, 6);
    if (z != 3) return 7;
    if (count_only[0] != 'q' || count_only[1] != 'r') return 8;

    char full[32];
    int v = fmt_vs(full, "%d-%u-%s-%c-%%", -7, 19u, "va", 90);
    if (v != 12) return 9;
    if (strcmp(full, "-7-19-va-Z-%") != 0) return 10;
    return 0;
}
