// WAT standalone wchar.h extended wide numeric conversions
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t *end = 0;
    wchar_t sll[] = L" -123x";
    wchar_t ull[] = L"2a!";
    wchar_t flt[] = L" 3.5z";
    wchar_t ld[] = L"-7.25!";
    wchar_t none[] = L" q";

    if (wcstoll(sll, &end, 10) != -123LL) return 1;
    if (end != sll + 5 || *end != L'x') return 2;

    if (wcstoull(ull, &end, 16) != 42ULL) return 3;
    if (end != ull + 2 || *end != L'!') return 4;

    if (wcstof(flt, &end) != 3.5f) return 5;
    if (end != flt + 4 || *end != L'z') return 6;

    if (wcstold(ld, &end) != -7.25L) return 7;
    if (end != ld + 5 || *end != L'!') return 8;

    end = 0;
    if (wcstoll(none, &end, 10) != 0) return 9;
    if (end != none) return 10;

    end = 0;
    if (wcstof(none, &end) != 0.0f) return 11;
    if (end != none) return 12;

    return 0;
}
