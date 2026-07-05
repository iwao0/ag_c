// Wasm standalone stdio.h printf/fprintf/vprintf/vfprintf count stubs
// Expected: exit=0
#include <stdarg.h>
#include <stdio.h>

static int call_vprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

static int call_vfprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int main(void) {
    FILE *stream = (FILE *)1;
    if (printf("x=%d\n", 42) != 5) return 1;
    if (printf("%d-%u-%%", -12, 34u) != 8) return 2;
    if (fprintf(stream, "%s:%c:%02d", "ok", 65, 7) != 7) return 3;
    if (fprintf(0, "bad") != 3) return 4;
    if (call_vprintf("%d-%s-%c", -5, "va", 90) != 7) return 5;
    if (call_vfprintf(stream, "[%02d]", 3) != 4) return 6;
    if (call_vfprintf(0, "bad") != 3) return 7;
    return 0;
}
