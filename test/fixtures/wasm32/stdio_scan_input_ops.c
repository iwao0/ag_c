// Wasm standalone stdio.h scanf/vscanf/fscanf/vfscanf no-input stubs
// Expected: exit=0
#include <stdarg.h>
#include <stdio.h>

static int call_vscanf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vscanf(fmt, ap);
    va_end(ap);
    return n;
}

static int call_vfscanf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfscanf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int main(void) {
    FILE *stream = (FILE *)1;
    int a = 77;
    char word[4] = "abc";

    if (scanf("%d", &a) != EOF) return 1;
    if (a != 77) return 2;

    if (fscanf(stream, "%3s", word) != EOF) return 3;
    if (word[0] != 'a' || word[1] != 'b' || word[2] != 'c' || word[3] != 0) return 4;

    if (call_vscanf("%d", &a) != EOF) return 5;
    if (a != 77) return 6;

    if (call_vfscanf(stream, "%d", &a) != EOF) return 7;
    if (a != 77) return 8;

    return 0;
}
