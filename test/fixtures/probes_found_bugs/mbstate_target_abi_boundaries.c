// wchar.h/uchar.h shared mbstate_t target ABI and guarded state boundaries.
// Expected: exit=0
#include <stddef.h>
#include <uchar.h>
#include <wchar.h>

struct guarded_mbstate {
    unsigned long before;
    mbstate_t state;
    unsigned long after;
};

static size_t (*mbrtowc_signature)(
    wchar_t *, const char *, size_t, mbstate_t *) = mbrtowc;
static size_t (*mbrtoc32_signature)(
    char32_t *, const char *, size_t, mbstate_t *) = mbrtoc32;
static int (*mbsinit_signature)(const mbstate_t *) = mbsinit;

int main(void) {
    struct guarded_mbstate guarded = {0};
    wchar_t wide = 0;
    char32_t scalar = 0;

#ifdef __wasm32__
    if (sizeof(mbstate_t) != 32 || _Alignof(mbstate_t) != 4) return 1;
#else
    if (sizeof(mbstate_t) != 128 || _Alignof(mbstate_t) != 8) return 2;
#endif

    guarded.before = 0x1122334455667788UL;
    guarded.after = 0x8877665544332211UL;

    if (!mbsinit_signature(&guarded.state)) return 3;
    if (mbrtowc_signature(&wide, "A", 1, &guarded.state) != 1 ||
        wide != L'A' || !mbsinit_signature(&guarded.state)) {
        return 4;
    }
    if (mbrtoc32_signature(&scalar, "B", 1, &guarded.state) != 1 ||
        scalar != (char32_t)'B' || !mbsinit_signature(&guarded.state)) {
        return 5;
    }
    if (guarded.before != 0x1122334455667788UL ||
        guarded.after != 0x8877665544332211UL) {
        return 6;
    }
    return 0;
}
