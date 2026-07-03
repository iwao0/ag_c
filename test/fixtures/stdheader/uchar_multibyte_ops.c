// uchar.h ASCII multibyte conversion runtime calls
// Expected: exit=0
#include <stddef.h>
#include <uchar.h>

#ifndef __wasm32__
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps) {
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    if (pc16) *pc16 = (unsigned char)s[0];
    return s[0] == 0 ? 0 : 1;
}

size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps) {
    (void)ps;
    if (!s) return 1;
    s[0] = (char)c16;
    return 1;
}

size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps) {
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    if (pc32) *pc32 = (unsigned char)s[0];
    return s[0] == 0 ? 0 : 1;
}

size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps) {
    (void)ps;
    if (!s) return 1;
    s[0] = (char)c32;
    return 1;
}
#endif

int main(void) {
    char16_t c16 = 0;
    char32_t c32 = 0;
    char out[4] = {0};
    if (mbrtoc16(&c16, "A", 1, 0) != 1 || c16 != 65) return 1;
    if (mbrtoc16(&c16, "", 1, 0) != 0 || c16 != 0) return 2;
    if (mbrtoc16(&c16, "B", 0, 0) != (size_t)-2) return 3;
    if (c16rtomb(out, 67, 0) != 1 || out[0] != 'C') return 4;
    if (mbrtoc32(&c32, "x", 1, 0) != 1 || c32 != 120) return 5;
    if (mbrtoc32(&c32, "", 1, 0) != 0 || c32 != 0) return 6;
    if (mbrtoc32(&c32, "y", 0, 0) != (size_t)-2) return 7;
    if (c32rtomb(out, 90, 0) != 1 || out[0] != 'Z') return 8;
    return 0;
}
