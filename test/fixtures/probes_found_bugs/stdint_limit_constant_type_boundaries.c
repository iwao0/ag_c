// stdint.h exact/least/fast types and limit/constant macro boundaries.
// Expected: exit=0
#include <stddef.h>
#include <stdint.h>

static int64_t signed_constant = INT64_C(0x123456789abcdef);
static uint64_t unsigned_constant = UINT64_C(0xfedcba9876543210);
static intmax_t signed_max_constant = INTMAX_C(0x123456789abcdef);
static uintmax_t unsigned_max_constant = UINTMAX_C(0xfedcba9876543210);

int main(void) {
    volatile int least8_kind = _Generic(
        (int_least8_t)0, signed char: 1, default: 0);
    volatile int least16_kind = _Generic(
        (int_least16_t)0, short: 1, default: 0);
    volatile int least32_kind = _Generic(
        (int_least32_t)0, int: 1, default: 0);
    volatile int least64_kind = _Generic(
        (int_least64_t)0, long long: 1, default: 0);
    volatile int fast8_kind = _Generic(
        (uint_fast8_t)0, unsigned char: 1, default: 0);
    volatile int fast16_kind = _Generic(
        (uint_fast16_t)0, unsigned short: 1, default: 0);
    volatile int fast32_kind = _Generic(
        (uint_fast32_t)0, unsigned int: 1, default: 0);
    volatile int fast64_kind = _Generic(
        (uint_fast64_t)0, unsigned long long: 1, default: 0);
    volatile int intptr_kind = _Generic(
        (intptr_t)0, long: 1, default: 0);
    volatile int uintptr_kind = _Generic(
        (uintptr_t)0, unsigned long: 1, default: 0);
    volatile int intmax_kind = _Generic(
        (intmax_t)0, long: 1, long long: 2, default: 0);
    volatile int uintmax_kind = _Generic(
        (uintmax_t)0, unsigned long: 1,
        unsigned long long: 2, default: 0);
    volatile int uint8_max_kind = _Generic(
        UINT8_MAX, int: 1, unsigned int: 2, default: 0);
    volatile int uint16_max_kind = _Generic(
        UINT16_MAX, int: 1, unsigned int: 2, default: 0);
    volatile int int32_min_kind = _Generic(
        INT32_MIN, int: 1, long: 2, default: 0);
    volatile int size_max_kind = _Generic(
        SIZE_MAX, unsigned long: 1,
        unsigned long long: 2, default: 0);
    volatile int int64_constant_kind = _Generic(
        INT64_C(1), long long: 1, default: 0);
    volatile int uint64_constant_kind = _Generic(
        UINT64_C(1), unsigned long long: 1, default: 0);
    volatile int intmax_constant_kind = _Generic(
        INTMAX_C(1), long: 1, long long: 2, default: 0);
    volatile int uintmax_constant_kind = _Generic(
        UINTMAX_C(1), unsigned long: 1,
        unsigned long long: 2, default: 0);
    int object = 7;
    uintptr_t address = (uintptr_t)&object;

    if (least8_kind != 1 || least16_kind != 1 ||
        least32_kind != 1 || least64_kind != 1 ||
        fast8_kind != 1 || fast16_kind != 1 ||
        fast32_kind != 1 || fast64_kind != 1 ||
        intptr_kind != 1 || uintptr_kind != 1)
        return 1;
#ifdef __wasm32__
    if (intmax_kind != 2 || uintmax_kind != 2 ||
        intmax_constant_kind != 2 || uintmax_constant_kind != 2)
        return 2;
#else
    if (intmax_kind != 1 || uintmax_kind != 1 ||
        intmax_constant_kind != 1 || uintmax_constant_kind != 1)
        return 3;
#endif
    if (uint8_max_kind != 1 || uint16_max_kind != 1 ||
        int32_min_kind != 1 || size_max_kind != 1 ||
        int64_constant_kind != 1 || uint64_constant_kind != 1)
        return 4;
    if (INT8_MIN != -128 || INT8_MAX != 127 || UINT8_MAX != 255 ||
        INT16_MIN != -32768 || INT16_MAX != 32767 ||
        UINT16_MAX != 65535 || INT32_MIN != (-2147483647 - 1) ||
        INT32_MAX != 2147483647 || UINT32_MAX != 4294967295U ||
        INT64_MIN != (-9223372036854775807LL - 1) ||
        INT64_MAX != 9223372036854775807LL ||
        UINT64_MAX != 18446744073709551615ULL)
        return 5;
    if (INT_LEAST8_MIN != INT8_MIN || INT_LEAST64_MAX != INT64_MAX ||
        UINT_LEAST16_MAX != UINT16_MAX ||
        INT_FAST32_MIN != INT32_MIN || UINT_FAST64_MAX != UINT64_MAX)
        return 6;
    if (INTPTR_MIN != (-9223372036854775807L - 1) ||
        INTPTR_MAX != 9223372036854775807L ||
        UINTPTR_MAX != 18446744073709551615UL ||
        PTRDIFF_MIN != (-9223372036854775807L - 1) ||
        PTRDIFF_MAX != 9223372036854775807L ||
        SIZE_MAX != 18446744073709551615UL)
        return 7;
    if (SIG_ATOMIC_MIN != (-2147483647 - 1) ||
        SIG_ATOMIC_MAX != 2147483647 ||
        WCHAR_MIN != (-2147483647 - 1) || WCHAR_MAX != 2147483647 ||
        WINT_MIN != (-2147483647 - 1) || WINT_MAX != 2147483647)
        return 8;
    if (signed_constant != (int64_t)0x123456789abcdefLL ||
        unsigned_constant != (uint64_t)0xfedcba9876543210ULL ||
        signed_max_constant != (intmax_t)0x123456789abcdefLL ||
        unsigned_max_constant != (uintmax_t)0xfedcba9876543210ULL)
        return 9;
    if (sizeof(intptr_t) < sizeof(void *) ||
        sizeof(uintptr_t) < sizeof(void *) ||
        (int *)address != &object || *(int *)address != 7)
        return 10;
    return 0;
}
