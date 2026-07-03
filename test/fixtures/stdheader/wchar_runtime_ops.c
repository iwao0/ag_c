// wchar.h wide string and memory runtime calls
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t src[] = L"abc";
    wchar_t dst[8];
    wchar_t tail[] = L"de";
    wchar_t mem[4];
    wmemset(dst, 0, 8);
    if (wcsncpy(dst, src, 4) != dst) return 1;
    if (wcsncmp(dst, L"abc", 3) != 0) return 2;
    if (wcscat(dst, tail) != dst) return 3;
    if (wcslen(dst) != 5) return 4;
    if (wcschr(dst, L'c') != dst + 2) return 5;
    if (wcsrchr(dst, L'd') != dst + 3) return 6;
    if (wmemcpy(mem, dst, 3) != mem) return 7;
    if (wcsncmp(mem, L"abc", 3) != 0) return 8;
    return 0;
}
