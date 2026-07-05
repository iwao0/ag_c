// WAT standalone string collation/transform stubs
// Expected: exit=0
#include <string.h>

int main(void) {
    char buf[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 0};
    char small[3] = {'q', 'q', 0};
    char untouched[2] = {'z', 0};

    if (strcoll("abc", "abc") != 0) return 1;
    if (strcoll("abc", "abd") >= 0) return 2;
    if (strcoll("abd", "abc") <= 0) return 3;
    if (strcoll("ab", "abc") >= 0) return 4;

    if (strxfrm(buf, "hello", sizeof(buf)) != 5) return 5;
    if (strcmp(buf, "hello") != 0) return 6;
    if (buf[6] != 'x') return 7;

    if (strxfrm(small, "abcd", sizeof(small)) != 4) return 8;
    if (small[0] != 'a' || small[1] != 'b' || small[2] != 0) return 9;

    if (strxfrm(untouched, "xy", 0) != 2) return 10;
    if (untouched[0] != 'z' || untouched[1] != 0) return 11;

    return 0;
}
