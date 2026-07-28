/*
 * strftime and wcsftime retain a size_t output limit above the wasm32 address
 * range while formatting a short result that ends well before that limit.
 * Expected: exit=0
 */
#include <stddef.h>
#include <time.h>
#include <wchar.h>

static size_t (*strftime_signature)(
    char *, size_t, const char *, const struct tm *) = strftime;
static size_t (*wcsftime_signature)(
    wchar_t *, size_t, const wchar_t *, const struct tm *) = wcsftime;

static int narrow_matches(const char *text) {
    static const char expected[] = "2024-07-28 13:05:09";
    int index = 0;
    while (expected[index]) {
        if (text[index] != expected[index]) return 0;
        index++;
    }
    return text[index] == '\0' && text[index + 1] == '?';
}

static int wide_matches(const wchar_t *text) {
    static const wchar_t expected[] = L"2024-07-28 13:05:09";
    int index = 0;
    while (expected[index]) {
        if (text[index] != expected[index]) return 0;
        index++;
    }
    return text[index] == L'\0' && text[index + 1] == L'?';
}

int main(void) {
    size_t above_32_bits = (size_t)4294967296ULL;
    struct tm value = {0};
    char narrow[32];
    wchar_t wide[32];
    int index;

    if (above_32_bits == 0) return 0;

    value.tm_sec = 9;
    value.tm_min = 5;
    value.tm_hour = 13;
    value.tm_mday = 28;
    value.tm_mon = 6;
    value.tm_year = 124;

    for (index = 0; index < 32; index++) {
        narrow[index] = '?';
        wide[index] = L'?';
    }

    if (strftime_signature(
            narrow, above_32_bits, "%Y-%m-%d %H:%M:%S", &value) != 19 ||
        !narrow_matches(narrow)) {
        return 1;
    }
    if (wcsftime_signature(
            wide, above_32_bits, L"%Y-%m-%d %H:%M:%S", &value) != 19 ||
        !wide_matches(wide)) {
        return 2;
    }
    return 0;
}
