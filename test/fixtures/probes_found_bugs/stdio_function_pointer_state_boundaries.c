#include <stddef.h>
#include <stdio.h>

int main(void) {
    int (*puts_fn)(const char *) = puts;
    int (*putchar_fn)(int) = putchar;
    int (*fputs_fn)(const char *, FILE *) = fputs;
    int (*fputc_fn)(int, FILE *) = fputc;
    int (*putc_fn)(int, FILE *) = putc;
    int (*getchar_fn)(void) = getchar;
    int (*fgetc_fn)(FILE *) = fgetc;
    int (*getc_fn)(FILE *) = getc;
    int (*ungetc_fn)(int, FILE *) = ungetc;
    char *(*fgets_fn)(char *, int, FILE *) = fgets;
    long (*getline_fn)(char **, size_t *, FILE *) = getline;
    FILE *(*fopen_fn)(const char *, const char *) = fopen;
    FILE *(*freopen_fn)(const char *, const char *, FILE *) = freopen;
    FILE *(*tmpfile_fn)(void) = tmpfile;
    char *(*tmpnam_fn)(char *) = tmpnam;
    FILE *(*fdopen_fn)(int, const char *) = fdopen;
    int (*fclose_fn)(FILE *) = fclose;
    int (*remove_fn)(const char *) = remove;
    int (*rename_fn)(const char *, const char *) = rename;
    int (*fflush_fn)(FILE *) = fflush;
    void (*setbuf_fn)(FILE *, char *) = setbuf;
    int (*setvbuf_fn)(FILE *, char *, int, size_t) = setvbuf;
    int (*fseek_fn)(FILE *, long, int) = fseek;
    long (*ftell_fn)(FILE *) = ftell;
    int (*fgetpos_fn)(FILE *, fpos_t *) = fgetpos;
    int (*fsetpos_fn)(FILE *, const fpos_t *) = fsetpos;
    void (*rewind_fn)(FILE *) = rewind;
    size_t (*fread_fn)(void *, size_t, size_t, FILE *) = fread;
    size_t (*fwrite_fn)(const void *, size_t, size_t, FILE *) = fwrite;
    void (*perror_fn)(const char *) = perror;
    int (*feof_fn)(FILE *) = feof;
    int (*ferror_fn)(FILE *) = ferror;
    void (*clearerr_fn)(FILE *) = clearerr;

    if (!puts_fn || !putchar_fn || !fputs_fn || !fputc_fn || !putc_fn ||
        !getchar_fn || !fgetc_fn || !getc_fn || !ungetc_fn || !fgets_fn ||
        !getline_fn || !fopen_fn || !freopen_fn || !tmpfile_fn || !tmpnam_fn ||
        !fdopen_fn || !fclose_fn || !remove_fn || !rename_fn || !fflush_fn ||
        !setbuf_fn || !setvbuf_fn || !fseek_fn || !ftell_fn || !fgetpos_fn ||
        !fsetpos_fn || !rewind_fn || !fread_fn || !fwrite_fn || !perror_fn ||
        !feof_fn || !ferror_fn || !clearerr_fn) {
        return 1;
    }

    FILE *stream = tmpfile_fn();
    if (!stream) {
        /*
         * Standalone WAT intentionally has no file store.  Exercise the same
         * exact function-pointer signatures against its unavailable-file
         * contract instead of pretending that it provides persistent files.
         */
        FILE *stub_stream = (FILE *)1;
        char buffer[8] = {0};
        char *line = 0;
        size_t capacity = 0;
        fpos_t position = 9;

        if (fopen_fn("missing", "r") != 0) return 2;
        if (freopen_fn("missing", "r", stub_stream) != 0) return 3;
        if (tmpnam_fn(buffer) != 0) return 4;
        if (fdopen_fn(3, "r") != 0) return 5;
        if (remove_fn("missing") != -1) return 6;
        if (rename_fn("old", "new") != -1) return 7;
        if (setvbuf_fn(stub_stream, 0, _IONBF, 0) != 0) return 8;
        setbuf_fn(stub_stream, 0);
        if (fputs_fn("xy", stub_stream) != 2) return 9;
        if (fputc_fn('A', stub_stream) != 'A') return 10;
        if (putc_fn('B', stub_stream) != 'B') return 11;
        if (fwrite_fn("xy", 1, 2, stub_stream) != 0) return 12;
        if (fread_fn(buffer, 1, sizeof(buffer), stub_stream) != 0) return 13;
        if (fgetc_fn(stub_stream) != EOF || getc_fn(stub_stream) != EOF) return 14;
        if (ungetc_fn('Q', stub_stream) != EOF) return 15;
        if (fgets_fn(buffer, sizeof(buffer), stub_stream) != 0) return 16;
        if (getline_fn(&line, &capacity, stub_stream) != -1 ||
            line != 0 || capacity != 0) {
            return 17;
        }
        if (getchar_fn() != EOF) return 18;
        if (fflush_fn(stub_stream) != 0) return 19;
        if (fseek_fn(stub_stream, 0, SEEK_SET) != 0) return 20;
        if (ftell_fn(stub_stream) != 0) return 21;
        if (fgetpos_fn(stub_stream, &position) != 0 || position != 0) return 22;
        if (fsetpos_fn(stub_stream, &position) != 0) return 23;
        rewind_fn(stub_stream);
        if (feof_fn(stub_stream) != 0 || ferror_fn(stub_stream) != 0) return 24;
        clearerr_fn(stub_stream);
        perror_fn("ignored");
        if (fclose_fn(stub_stream) != 0) return 25;
        return 0;
    }

    if (setvbuf_fn(stream, 0, _IONBF, 0) != 0) return 26;
    if (fwrite_fn("ignored", 0, (size_t)-1, stream) != 0) return 27;
    if (ftell_fn(stream) != 0) return 28;
    if (fputs_fn("A\n", stream) == EOF) return 29;
    if (fputc_fn(0, stream) != 0) return 30;
    if (putc_fn(0x7f, stream) != 0x7f) return 31;

    {
        const unsigned char tail[] = {0x80, 0xff, 'Z'};
        if (fwrite_fn(tail, 1, sizeof(tail), stream) != sizeof(tail)) return 32;
    }
    if (fflush_fn(stream) != 0 || ftell_fn(stream) != 7) return 33;

    fpos_t end_position = -1;
    if (fgetpos_fn(stream, &end_position) != 0 || end_position != 7) return 34;

    rewind_fn(stream);
    if (ftell_fn(stream) != 0 || feof_fn(stream) || ferror_fn(stream)) return 35;

    {
        unsigned char bytes[4] = {0};
        if (fread_fn(bytes, 0, (size_t)-1, stream) != 0) return 36;
        if (ftell_fn(stream) != 0) return 37;
    }

    char line[4] = {0};
    if (fgets_fn(line, sizeof(line), stream) != line) return 38;
    if (line[0] != 'A' || line[1] != '\n' || line[2] != 0) return 39;

    fpos_t data_position = -1;
    if (fgetpos_fn(stream, &data_position) != 0 || data_position != 2) return 40;
    if (fgetc_fn(stream) != 0) return 41;
    if (ungetc_fn('Q', stream) != 'Q' || getc_fn(stream) != 'Q') return 42;
    if (fsetpos_fn(stream, &data_position) != 0 || feof_fn(stream)) return 43;
    if (fgetc_fn(stream) != 0) return 44;

    {
        unsigned char bytes[4] = {0};
        if (fread_fn(bytes, 2, 2, stream) != 2) return 45;
        if (bytes[0] != 0x7f || bytes[1] != 0x80 ||
            bytes[2] != 0xff || bytes[3] != 'Z') {
            return 46;
        }
    }
    if (fgetc_fn(stream) != EOF || !feof_fn(stream) || ferror_fn(stream)) return 47;
    clearerr_fn(stream);
    if (feof_fn(stream) || ferror_fn(stream)) return 48;

    if (fseek_fn(stream, -1, SEEK_END) != 0 || ftell_fn(stream) != 6) return 49;
    if (getc_fn(stream) != 'Z' || getc_fn(stream) != EOF || !feof_fn(stream)) return 50;
    if (fsetpos_fn(stream, &end_position) != 0 || feof_fn(stream)) return 51;
    if (ftell_fn(stream) != 7) return 52;

    rewind_fn(stream);
    if (fgetc_fn(stream) != 'A' || feof_fn(stream) || ferror_fn(stream)) return 53;
    if (fclose_fn(stream) != 0) return 54;
    return 0;
}
