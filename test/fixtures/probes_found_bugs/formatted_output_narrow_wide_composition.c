#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void) {
    char narrow[32];
    wchar_t wide[32];

    if (snprintf(narrow, sizeof(narrow), "%#.1f/%#x", 1.5, 0x2a) != 8) return 1;
    if (strcmp(narrow, "1.5/0x2a") != 0) return 2;

    if (swprintf(wide, 32, L"%#.1f/%#x", 2.5, 0x3b) != 8) return 3;
    if (wcscmp(wide, L"2.5/0x3b") != 0) return 4;

    if (snprintf(narrow, sizeof(narrow), "%.2e", 0.125) != 8) return 5;
    if (strcmp(narrow, "1.25e-01") != 0) return 6;

    return 0;
}
