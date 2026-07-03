// wchar.h wide memory runtime calls
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t buf[8] = L"abcde";
    if (wmemmove(buf + 1, buf, 3) != buf + 1) return 1;
    if (wmemcmp(buf, L"aabce", 5) != 0) return 2;
    if (wmemchr(buf, L'c', 5) != buf + 3) return 3;
    if (wmemchr(buf, L'z', 5) != 0) return 4;
    return 0;
}
