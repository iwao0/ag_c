// wchar.h wide numeric conversion runtime calls
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t *end = 0;
    wchar_t signed_src[] = L"  -2aZ";
    wchar_t unsigned_src[] = L"+1012";
    wchar_t float_src[] = L" -12.5x";
    if (wcstol(signed_src, &end, 16) != -42) return 1;
    if (end != signed_src + 5 || *end != L'Z') return 2;
    if (wcstoul(unsigned_src, &end, 2) != 5) return 3;
    if (end != unsigned_src + 4 || *end != L'2') return 4;
    if (wcstod(float_src, &end) != -12.5) return 5;
    if (end != float_src + 6 || *end != L'x') return 6;
    return 0;
}
