// inttypes.h target-specific intmax_t formats and aggregate member types.
// Expected: exit=0
#include <inttypes.h>

static int same_text(const char *left, const char *right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

int main(void) {
    const char *max_formats[] = {
        PRIdMAX, PRIiMAX, PRIoMAX, PRIuMAX, PRIxMAX, PRIXMAX,
        SCNdMAX, SCNiMAX, SCNoMAX, SCNuMAX, SCNxMAX
    };
    const char *max_suffixes[] = {
        "d", "i", "o", "u", "x", "X", "d", "i", "o", "u", "x"
    };
    const char *pointer_formats[] = {
        PRIdPTR, PRIiPTR, PRIoPTR, PRIuPTR, PRIxPTR, PRIXPTR,
        SCNdPTR, SCNiPTR, SCNoPTR, SCNuPTR, SCNxPTR
    };
    const char *wide_formats[] = {
        PRId64, PRIi64, PRIo64, PRIu64, PRIx64, PRIX64,
        SCNd64, SCNi64, SCNo64, SCNu64, SCNx64
    };
    volatile int quotient_kind = _Generic(
        ((imaxdiv_t *)0)->quot, long: 1, long long: 2, default: 0);
    int index;

    for (index = 0; index < 11; index++) {
        char expected_max[4];
        char expected_pointer[3] = {'l', 0, 0};
        char expected_wide[4] = {'l', 'l', 0, 0};
#ifdef __wasm32__
        expected_max[0] = 'l';
        expected_max[1] = 'l';
        expected_max[2] = max_suffixes[index][0];
        expected_max[3] = 0;
#else
        expected_max[0] = 'l';
        expected_max[1] = max_suffixes[index][0];
        expected_max[2] = 0;
        expected_max[3] = 0;
#endif
        expected_pointer[1] = max_suffixes[index][0];
        expected_wide[2] = max_suffixes[index][0];
        if (!same_text(max_formats[index], expected_max) ||
            !same_text(pointer_formats[index], expected_pointer) ||
            !same_text(wide_formats[index], expected_wide))
            return index + 1;
    }
#ifdef __wasm32__
    if (quotient_kind != 2)
        return 20;
#else
    if (quotient_kind != 1)
        return 21;
#endif
    return 0;
}
