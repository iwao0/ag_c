// stdio.h fpos_t target type identity and guarded position boundaries.
// Expected: exit=0
#include <stdio.h>

struct guarded_position {
    unsigned long before;
    fpos_t position;
    unsigned long after;
};

static int (*fgetpos_signature)(FILE *, fpos_t *) = fgetpos;
static int (*fsetpos_signature)(FILE *, const fpos_t *) = fsetpos;

int main(void) {
    struct guarded_position guarded = {0};
    FILE *stream;
    volatile int position_kind = _Generic(
        (fpos_t)0,
        long: 1,
        long long: 2,
        default: 0);

#ifdef __wasm32__
    if (position_kind != 1 || sizeof(fpos_t) != 8) return 1;
#else
    if (position_kind != 2 || sizeof(fpos_t) != 8) return 2;
#endif

    guarded.before = 0x1122334455667788UL;
    guarded.position = -1;
    guarded.after = 0x8877665544332211UL;

    stream = tmpfile();
    if (!stream) {
        FILE *stub_stream = (FILE *)1;
        if (fgetpos_signature(stub_stream, &guarded.position) != 0 ||
            guarded.position != 0 ||
            fsetpos_signature(stub_stream, &guarded.position) != 0) {
            return 3;
        }
    } else {
        if (fputs("abc", stream) == EOF ||
            fgetpos_signature(stream, &guarded.position) != 0 ||
            guarded.position != 3) {
            return 4;
        }
        rewind(stream);
        if (fsetpos_signature(stream, &guarded.position) != 0 ||
            ftell(stream) != 3 ||
            fclose(stream) != 0) {
            return 5;
        }
    }

    if (guarded.before != 0x1122334455667788UL ||
        guarded.after != 0x8877665544332211UL) {
        return 6;
    }
    return 0;
}
