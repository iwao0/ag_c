// wchar.h wcsftime runtime call
// Expected: exit=0
#include <time.h>
#include <wchar.h>

static int wide_eq(const wchar_t *a, const wchar_t *b) {
    while (*a || *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return 1;
}

int main(void) {
    time_t t = 1609459200;
    struct tm *tm = gmtime(&t);
    wchar_t buf[96];
    wchar_t small[4];
    size_t n;
    if (!tm) return 1;
    n = wcsftime(buf, sizeof(buf) / sizeof(buf[0]), L"%G-%V %A %B %F %T", tm);
    if (!wide_eq(buf, L"2020-53 Friday January 2021-01-01 00:00:00")) return 2;
    if (n != 42) return 3;
    if (wcsftime(small, sizeof(small) / sizeof(small[0]), L"%Y-%m-%d", tm) != 0) return 4;
    return 0;
}
