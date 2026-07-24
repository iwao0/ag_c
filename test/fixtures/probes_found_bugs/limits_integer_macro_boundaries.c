// limits.h integer macro completeness, values, expression types, and #if use.
// Expected: exit=0
#include <limits.h>

#if CHAR_BIT != 8
#error "CHAR_BIT must describe an 8-bit byte"
#endif

#if SCHAR_MIN != -128 || SCHAR_MAX != 127 || UCHAR_MAX != 255
#error "plain character limits must be usable by the preprocessor"
#endif

#if SHRT_MIN != -32768 || SHRT_MAX != 32767 || USHRT_MAX != 65535
#error "short limits must be usable by the preprocessor"
#endif

#if INT_MIN != (-2147483647 - 1) || INT_MAX != 2147483647
#error "int limits must be usable by the preprocessor"
#endif

#ifdef __wasm32__
#if MB_LEN_MAX != 4
#error "the Wasm UTF-8 boundary must allow four-byte sequences"
#endif
#else
#if MB_LEN_MAX != 6
#error "the native Darwin multibyte ABI limit must be preserved"
#endif
#endif

static char multibyte_buffer[MB_LEN_MAX];
static long long_min_initializer = LONG_MIN;
static unsigned long ulong_max_initializer = ULONG_MAX;
static long long llong_min_initializer = LLONG_MIN;
static unsigned long long ullong_max_initializer = ULLONG_MAX;

#define IS_INT(value) _Generic((value), int: 1, default: 0)

int main(void) {
    volatile int small_limit_types =
        IS_INT(CHAR_BIT) + IS_INT(MB_LEN_MAX) +
        IS_INT(SCHAR_MIN) + IS_INT(SCHAR_MAX) + IS_INT(UCHAR_MAX) +
        IS_INT(CHAR_MIN) + IS_INT(CHAR_MAX) +
        IS_INT(SHRT_MIN) + IS_INT(SHRT_MAX) + IS_INT(USHRT_MAX) +
        IS_INT(INT_MIN) + IS_INT(INT_MAX);
    volatile int uint_kind = _Generic(
        UINT_MAX, unsigned int: 1, default: 0);
    volatile int long_min_kind = _Generic(
        LONG_MIN, long: 1, default: 0);
    volatile int long_max_kind = _Generic(
        LONG_MAX, long: 1, default: 0);
    volatile int ulong_kind = _Generic(
        ULONG_MAX, unsigned long: 1, default: 0);
    volatile int llong_min_kind = _Generic(
        LLONG_MIN, long long: 1, default: 0);
    volatile int llong_max_kind = _Generic(
        LLONG_MAX, long long: 1, default: 0);
    volatile int ullong_kind = _Generic(
        ULLONG_MAX, unsigned long long: 1, default: 0);

    if (small_limit_types != 12 || uint_kind != 1 ||
        long_min_kind != 1 || long_max_kind != 1 || ulong_kind != 1 ||
        llong_min_kind != 1 || llong_max_kind != 1 || ullong_kind != 1)
        return 1;
    if (CHAR_MIN != SCHAR_MIN || CHAR_MAX != SCHAR_MAX ||
        UCHAR_MAX != (1U << CHAR_BIT) - 1U)
        return 2;
    if (INT_MIN + INT_MAX != -1 || UINT_MAX != 4294967295U)
        return 3;
    if (LONG_MIN + LONG_MAX != -1L ||
        ULONG_MAX != 18446744073709551615UL)
        return 4;
    if (LLONG_MIN + LLONG_MAX != -1LL ||
        ULLONG_MAX != 18446744073709551615ULL)
        return 5;
    if (sizeof(multibyte_buffer) != MB_LEN_MAX ||
        long_min_initializer != LONG_MIN ||
        ulong_max_initializer != ULONG_MAX ||
        llong_min_initializer != LLONG_MIN ||
        ullong_max_initializer != ULLONG_MAX)
        return 6;
    return 0;
}
