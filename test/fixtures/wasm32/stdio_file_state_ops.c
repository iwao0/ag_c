// Wasm standalone stdio.h file/state stubs
// Expected: exit=0
#include <stdio.h>

int main(void) {
    FILE *stream = (FILE *)1;
    char buf[8] = "keep";
    char *line = 0;
    size_t cap = 0;
    fpos_t pos = 99;

    if (fopen("missing", "r") != 0) return 1;
    if (freopen("missing", "r", stream) != 0) return 2;
    if (tmpfile() != 0) return 3;
    if (tmpnam(buf) != 0) return 4;
    if (buf[0] != 'k') return 5;
    if (fdopen(3, "r") != 0) return 6;
    if (remove("missing") != -1) return 7;
    if (rename("a", "b") != -1) return 8;

    if (fflush(stream) != 0) return 9;
    if (fflush(0) != 0) return 10;
    if (getchar() != EOF) return 11;
    if (ungetc('x', stream) != EOF) return 12;
    if (getline(&line, &cap, stream) != -1) return 13;
    if (line != 0 || cap != 0) return 14;

    if (fseek(stream, 4, SEEK_SET) != 0) return 15;
    if (fseek(0, 0, SEEK_SET) != -1) return 16;
    if (fseek(stream, 0, 99) != -1) return 17;
    if (ftell(stream) != 0) return 18;
    if (ftell(0) != -1) return 19;
    if (fgetpos(stream, &pos) != 0) return 20;
    if (pos != 0) return 21;
    pos = 7;
    if (fgetpos(0, &pos) != -1 || pos != 7) return 22;
    if (fsetpos(stream, &pos) != 0) return 23;
    if (fsetpos(stream, 0) != -1) return 24;
    rewind(stream);

    if (feof(stream) != 0) return 25;
    if (ferror(stream) != 0) return 26;
    clearerr(stream);
    if (fclose(stream) != 0) return 27;

    return 0;
}
