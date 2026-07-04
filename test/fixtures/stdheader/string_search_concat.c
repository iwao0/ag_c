// string.h search and bounded concat helpers
// Expected: exit=0
#include <assert.h>
#include <string.h>

int main(void) {
    char buf[16] = "ab";
    assert(strncat(buf, "cdef", 2) == buf);
    assert(strcmp(buf, "abcd") == 0);
    assert(strncat(buf, "XYZ", 0) == buf);
    assert(strcmp(buf, "abcd") == 0);

    const char *s = "hello";
    assert(memchr(s, 'e', 5) == s + 1);
    assert(memchr(s, 'z', 5) == 0);
    assert(memchr(s, 'l' + 256, 5) == s + 2);

    const char *haystack = "bananarama";
    assert(strstr(haystack, "ana") == haystack + 1);
    assert(strstr(haystack, "xyz") == 0);
    assert(strstr(haystack, "") == haystack);

    char pbrk_src[] = "xyzabc";
    assert(strspn("aabbc", "ab") == 4);
    assert(strspn("aabbc", "") == 0);
    assert(strcspn("aabbc", "c") == 4);
    assert(strcspn("aabbc", "") == 5);
    assert(strpbrk(pbrk_src, "ba") == pbrk_src + 3);
    assert(strpbrk("xyz", "ab") == 0);
    return 0;
}
