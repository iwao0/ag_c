// Wasm standalone stdio.h sscanf/vsscanf stubs
// Expected: exit=0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int call_vsscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}

int main(void) {
    int a = 0;
    unsigned b = 0;
    char word[8];
    char ch = 0;

    if (sscanf(" -12:34", " %d:%u", &a, &b) != 2) return 1;
    if (a != -12 || b != 34u) return 2;

    if (sscanf("wide Z", "%s %c", word, &ch) != 2) return 3;
    if (strcmp(word, "wide") != 0 || ch != 'Z') return 4;

    a = 99;
    b = 77;
    if (sscanf("x", "%d:%u", &a, &b) != 0) return 5;
    if (a != 99 || b != 77) return 6;

    a = 0;
    b = 0;
    if (call_vsscanf("7/8", "%d/%u", &a, &b) != 2) return 7;
    if (a != 7 || b != 8u) return 8;
    return 0;
}
