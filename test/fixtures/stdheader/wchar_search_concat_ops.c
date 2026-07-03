// wchar.h wide search and bounded concat runtime calls
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t buf[16] = L"ab";
    if (wcsncat(buf, L"cdef", 2) != buf) return 1;
    if (wcscmp(buf, L"abcd") != 0) return 2;
    if (wcsncat(buf, L"XY", 0) != buf) return 3;
    if (wcscmp(buf, L"abcd") != 0) return 4;
    if (wcsstr(buf, L"bc") != buf + 1) return 5;
    if (wcsstr(buf, L"") != buf) return 6;
    if (wcsstr(buf, L"zz") != 0) return 7;
    return 0;
}
