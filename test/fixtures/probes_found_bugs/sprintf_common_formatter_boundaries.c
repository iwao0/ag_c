#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void) {
    char buffer[128];
    int count = -1;

    if (sprintf(buffer, "%#08x/%+.2f/%-5s%n", 0x2a, 1.5, "xy", &count) != 20) return 1;
    if (strcmp(buffer, "0x00002a/+1.50/xy   ") != 0) return 2;
    if (count != 20) return 3;

    if (sprintf(buffer, "%#o/%.3ls/%5lc", 052, L"wide", L'Q') != 13) return 4;
    if (strcmp(buffer, "052/wid/    Q") != 0) return 5;

    if (sprintf(buffer, "%.2e/%#.0a", 0.125, 0.0) != 16) return 6;
    if (strcmp(buffer, "1.25e-01/0x0.p+0") != 0) return 7;

    return 0;
}
