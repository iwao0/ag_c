// Wasm standalone stdio output-only stubs
// Expected: exit=0
#include <stdio.h>

int main(void) {
    FILE *stream = (FILE *)1;
    char buf[8];
    if (puts("abc") != 4) return 1;
    if (puts(0) != EOF) return 2;
    if (fputs("wide", stream) != 4) return 3;
    if (fputs("wide", 0) != EOF) return 4;
    if (fputc('A', stream) != 'A') return 5;
    if (putc('B', stream) != 'B') return 6;
    if (fputc('Z', 0) != EOF) return 7;
    if (putc('Z', 0) != EOF) return 8;
    if (setvbuf(stream, buf, _IOFBF, sizeof(buf)) != 0) return 9;
    if (setvbuf(stream, 0, _IONBF, 0) != 0) return 10;
    if (setvbuf(stream, buf, 99, sizeof(buf)) != EOF) return 11;
    if (setvbuf(0, buf, _IOFBF, sizeof(buf)) != EOF) return 12;
    setbuf(stream, 0);
    perror("ignored");
    return 0;
}
