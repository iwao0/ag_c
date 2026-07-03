// string.h memmove overlap
// Expected: exit=0
#include <assert.h>
#include <string.h>

int main(void) {
    char a[12] = "abcdef";
    assert(memmove(a + 2, a, 5) == a + 2);
    assert(strcmp(a, "ababcde") == 0);

    char b[12] = "abcdef";
    assert(memmove(b, b + 2, 4) == b);
    assert(strcmp(b, "cdefef") == 0);

    char c[12] = "abcdef";
    assert(memmove(c, c, 6) == c);
    assert(strcmp(c, "abcdef") == 0);
    return 0;
}
