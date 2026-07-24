// stdio.h required integer macros and simultaneous stream boundary.
// Expected: exit=0
#include <stdio.h>

#if EOF != -1
#error "EOF must be a negative integer constant expression"
#endif

#if SEEK_SET != 0 || SEEK_CUR != 1 || SEEK_END != 2
#error "seek-origin macros must match the runtime contract"
#endif

#if _IOFBF != 0 || _IOLBF != 1 || _IONBF != 2
#error "buffering-mode macros must match the runtime contract"
#endif

#ifdef __wasm32__
#if BUFSIZ != 8192 || FOPEN_MAX != 11 || FILENAME_MAX != 64
#error "Wasm stdio capacity macros must match the bundled runtime"
#endif
#if L_tmpnam != 32 || TMP_MAX != 10000
#error "Wasm temporary-name limits must match the bundled runtime"
#endif
#else
#if BUFSIZ != 1024 || FOPEN_MAX != 20 || FILENAME_MAX != 1024
#error "native stdio capacity macros must match Darwin libc"
#endif
#if L_tmpnam != 1024 || TMP_MAX != 308915776
#error "native temporary-name limits must match Darwin libc"
#endif
#endif

static char io_buffer[BUFSIZ];
static char filename_buffer[FILENAME_MAX];
static char temporary_name[L_tmpnam];
static FILE *streams[FOPEN_MAX - 3];

#define IS_INT(value) _Generic((value), int: 1, default: 0)

int main(void) {
    volatile int macro_types =
        IS_INT(EOF) + IS_INT(SEEK_SET) + IS_INT(SEEK_CUR) +
        IS_INT(SEEK_END) + IS_INT(BUFSIZ) + IS_INT(FOPEN_MAX) +
        IS_INT(FILENAME_MAX) + IS_INT(L_tmpnam) + IS_INT(TMP_MAX) +
        IS_INT(_IOFBF) + IS_INT(_IOLBF) + IS_INT(_IONBF);
    const char *name = "agc_stdio_macro_capacity.tmp";
    int opened = 0;

    if (macro_types != 12 ||
        sizeof(io_buffer) != BUFSIZ ||
        sizeof(filename_buffer) != FILENAME_MAX ||
        sizeof(temporary_name) != L_tmpnam ||
        sizeof(streams) != (FOPEN_MAX - 3) * sizeof(FILE *))
        return 1;
    if (FOPEN_MAX < 8 || FILENAME_MAX < L_tmpnam || TMP_MAX < 25)
        return 2;

    remove(name);
    for (int i = 0; i < FOPEN_MAX - 3; ++i) {
        streams[i] = fopen(name, i == 0 ? "w+" : "r+");
        if (!streams[i])
            break;
        opened++;
    }
    if (opened == 0) {
        /* Standalone WAT intentionally exposes unavailable-file stubs. */
        return 0;
    }
    if (opened != FOPEN_MAX - 3)
        return 3;
    for (int i = opened; i > 0; --i) {
        if (fclose(streams[i - 1]) != 0)
            return 4;
    }
    if (remove(name) != 0)
        return 5;
    return 0;
}
