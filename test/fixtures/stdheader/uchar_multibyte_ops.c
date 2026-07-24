// uchar.h ASCII multibyte conversion runtime calls
// Expected: exit=0
#include <stddef.h>
#include <uchar.h>

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
