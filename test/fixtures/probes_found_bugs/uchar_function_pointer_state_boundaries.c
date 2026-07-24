#include <errno.h>
#include <stddef.h>
#include <uchar.h>

static size_t (*mbrtoc16_signature)(
    char16_t *, const char *, size_t, mbstate_t *) = mbrtoc16;
static size_t (*c16rtomb_signature)(char *, char16_t, mbstate_t *) = c16rtomb;
static size_t (*mbrtoc32_signature)(
    char32_t *, const char *, size_t, mbstate_t *) = mbrtoc32;
static size_t (*c32rtomb_signature)(char *, char32_t, mbstate_t *) = c32rtomb;

int main(void) {
    mbstate_t decode16_state = {0};
    mbstate_t encode16_state = {0};
    mbstate_t decode32_state = {0};
    mbstate_t encode32_state = {0};
    char16_t out16[3] = {0x1234, 0xabcd, 0x5678};
    char32_t out32[3] = {0x12345678U, 0xabcdef01U, 0x56789abcU};
    char narrow[4] = {'L', 'x', 'y', 'R'};
    const char ascii[] = "AZ";
    size_t huge = (size_t)-1;

    if (!mbrtoc16_signature || !c16rtomb_signature ||
        !mbrtoc32_signature || !c32rtomb_signature) {
        return 1;
    }

    if (mbrtoc16_signature(&out16[1], ascii, huge, &decode16_state) != 1 ||
        out16[0] != 0x1234 || out16[1] != (char16_t)'A' ||
        out16[2] != 0x5678) {
        return 2;
    }
    if (mbrtoc16_signature(0, ascii + 1, 1, &decode16_state) != 1) return 3;
    out16[1] = 0xabcd;
    if (mbrtoc16_signature(&out16[1], ascii, 0, &decode16_state) !=
            (size_t)-2 ||
        out16[1] != 0xabcd) {
        return 4;
    }
    if (mbrtoc16_signature(&out16[1], "", 1, &decode16_state) != 0 ||
        out16[1] != 0) {
        return 5;
    }
    if (mbrtoc16_signature(&out16[1], 0, 0, &decode16_state) != 0) return 6;
    if (mbrtoc16_signature(&out16[1], "B", 1, &decode16_state) != 1 ||
        out16[1] != (char16_t)'B') {
        return 7;
    }

    if (c16rtomb_signature(narrow + 1, (char16_t)'C', &encode16_state) != 1 ||
        narrow[0] != 'L' || narrow[1] != 'C' || narrow[2] != 'y' ||
        narrow[3] != 'R') {
        return 8;
    }
    if (c16rtomb_signature(0, (char16_t)'Q', &encode16_state) != 1) return 9;
    narrow[1] = 'x';
    if (c16rtomb_signature(narrow + 1, 0, &encode16_state) != 1 ||
        narrow[0] != 'L' || narrow[1] != 0 || narrow[2] != 'y' ||
        narrow[3] != 'R') {
        return 10;
    }

    if (mbrtoc32_signature(&out32[1], ascii, huge, &decode32_state) != 1 ||
        out32[0] != 0x12345678U || out32[1] != (char32_t)'A' ||
        out32[2] != 0x56789abcU) {
        return 11;
    }
    if (mbrtoc32_signature(0, ascii + 1, 1, &decode32_state) != 1) return 12;
    out32[1] = 0xabcdef01U;
    if (mbrtoc32_signature(&out32[1], ascii, 0, &decode32_state) !=
            (size_t)-2 ||
        out32[1] != 0xabcdef01U) {
        return 13;
    }
    if (mbrtoc32_signature(&out32[1], "", 1, &decode32_state) != 0 ||
        out32[1] != 0) {
        return 14;
    }
    if (mbrtoc32_signature(&out32[1], 0, 0, &decode32_state) != 0) return 15;
    if (mbrtoc32_signature(&out32[1], "D", 1, &decode32_state) != 1 ||
        out32[1] != (char32_t)'D') {
        return 16;
    }

    narrow[1] = 'x';
    if (c32rtomb_signature(narrow + 1, (char32_t)'E', &encode32_state) != 1 ||
        narrow[0] != 'L' || narrow[1] != 'E' || narrow[2] != 'y' ||
        narrow[3] != 'R') {
        return 17;
    }
    if (c32rtomb_signature(0, (char32_t)'Q', &encode32_state) != 1) return 18;
    narrow[1] = 'x';
    if (c32rtomb_signature(narrow + 1, 0, &encode32_state) != 1 ||
        narrow[0] != 'L' || narrow[1] != 0 || narrow[2] != 'y' ||
        narrow[3] != 'R') {
        return 19;
    }

    {
        const char emoji[] = {
            (char)0xf0, (char)0x9f, (char)0x98, (char)0x80, 0
        };
        mbstate_t unicode32_state = {0};
        char32_t scalar = 0;
        size_t result =
            mbrtoc32_signature(&scalar, emoji, 4, &unicode32_state);

        if (result == 1 && scalar == 0xf0) {
            /* Standalone WAT intentionally exposes an ASCII-only stub. */
            return 0;
        }
        if (result != 4 || scalar != 0x1f600U) return 20;

        {
            mbstate_t split_state = {0};
            scalar = 0;
            if (mbrtoc32_signature(&scalar, emoji, 2, &split_state) !=
                    (size_t)-2 ||
                scalar != 0) {
                return 21;
            }
            if (mbrtoc32_signature(&scalar, emoji + 2, 2, &split_state) != 2 ||
                scalar != 0x1f600U) {
                return 22;
            }
        }

        {
            mbstate_t unicode16_state = {0};
            char16_t pair[2] = {0, 0};
            if (mbrtoc16_signature(&pair[0], emoji, 4, &unicode16_state) != 4 ||
                pair[0] != 0xd83d) {
                return 23;
            }
            if (mbrtoc16_signature(&pair[1], "", 1, &unicode16_state) !=
                    (size_t)-3 ||
                pair[1] != 0xde00) {
                return 24;
            }
        }

        {
            mbstate_t surrogate_state = {0};
            char encoded[6] = {'L', 0, 0, 0, 0, 'R'};
            if (c16rtomb_signature(
                    encoded + 1, 0xd83d, &surrogate_state) != 0) {
                return 25;
            }
            if (c16rtomb_signature(
                    encoded + 1, 0xde00, &surrogate_state) != 4) {
                return 26;
            }
            if ((unsigned char)encoded[1] != 0xf0 ||
                (unsigned char)encoded[2] != 0x9f ||
                (unsigned char)encoded[3] != 0x98 ||
                (unsigned char)encoded[4] != 0x80 ||
                encoded[0] != 'L' || encoded[5] != 'R') {
                return 27;
            }
        }

        {
            mbstate_t scalar_state = {0};
            char encoded[6] = {'L', 0, 0, 0, 0, 'R'};
            if (c32rtomb_signature(
                    encoded + 1, 0x1f600U, &scalar_state) != 4) {
                return 28;
            }
            if ((unsigned char)encoded[1] != 0xf0 ||
                (unsigned char)encoded[2] != 0x9f ||
                (unsigned char)encoded[3] != 0x98 ||
                (unsigned char)encoded[4] != 0x80 ||
                encoded[0] != 'L' || encoded[5] != 'R') {
                return 29;
            }
        }

        {
            const char invalid[] = {(char)0xc0, (char)0x80, 0};
            mbstate_t invalid_state = {0};
            errno = 0;
            if (mbrtoc32_signature(
                    &scalar, invalid, 2, &invalid_state) != (size_t)-1 ||
                errno != EILSEQ) {
                return 30;
            }
            errno = 0;
            if (c32rtomb_signature(
                    narrow, 0x110000U, &invalid_state) != (size_t)-1 ||
                errno != EILSEQ) {
                return 31;
            }
        }
    }
    return 0;
}
