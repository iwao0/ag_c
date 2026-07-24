// wchar.h wide string and memory signatures through indirect calls.
// Expected: exit=0
#include <stddef.h>
#include <wchar.h>

static size_t (*wcslen_signature)(const wchar_t *) = wcslen;
static wchar_t *(*wcscpy_signature)(wchar_t *, const wchar_t *) = wcscpy;
static wchar_t *(*wcsncpy_signature)(wchar_t *, const wchar_t *, size_t) = wcsncpy;
static wchar_t *(*wcscat_signature)(wchar_t *, const wchar_t *) = wcscat;
static wchar_t *(*wcsncat_signature)(wchar_t *, const wchar_t *, size_t) = wcsncat;
static int (*wcscmp_signature)(const wchar_t *, const wchar_t *) = wcscmp;
static int (*wcsncmp_signature)(const wchar_t *, const wchar_t *, size_t) = wcsncmp;
static int (*wcscoll_signature)(const wchar_t *, const wchar_t *) = wcscoll;
static size_t (*wcsxfrm_signature)(wchar_t *, const wchar_t *, size_t) = wcsxfrm;
static wchar_t *(*wcschr_signature)(const wchar_t *, wchar_t) = wcschr;
static wchar_t *(*wcsrchr_signature)(const wchar_t *, wchar_t) = wcsrchr;
static wchar_t *(*wcsstr_signature)(const wchar_t *, const wchar_t *) = wcsstr;
static size_t (*wcsspn_signature)(const wchar_t *, const wchar_t *) = wcsspn;
static size_t (*wcscspn_signature)(const wchar_t *, const wchar_t *) = wcscspn;
static wchar_t *(*wcspbrk_signature)(const wchar_t *, const wchar_t *) = wcspbrk;
static wchar_t *(*wcstok_signature)(wchar_t *, const wchar_t *, wchar_t **) = wcstok;
static wchar_t *(*wmemcpy_signature)(wchar_t *, const wchar_t *, size_t) = wmemcpy;
static wchar_t *(*wmemmove_signature)(wchar_t *, const wchar_t *, size_t) = wmemmove;
static wchar_t *(*wmemset_signature)(wchar_t *, wchar_t, size_t) = wmemset;
static int (*wmemcmp_signature)(const wchar_t *, const wchar_t *, size_t) = wmemcmp;
static wchar_t *(*wmemchr_signature)(const wchar_t *, wchar_t, size_t) = wmemchr;

int main(void) {
    wchar_t buffer[24];
    wchar_t padded[6] = {L'x', L'x', L'x', L'x', L'x', L'x'};
    wchar_t transformed[8] = {L'x', L'x', L'x', L'x', L'x', L'x', L'x', L'x'};
    wchar_t words[] = L",alpha,,beta";
    wchar_t memory[8] = {L'a', L'b', L'c', L'd', L'e', 0, 0, 0};
    wchar_t copy[8] = {0};
    const wchar_t repeated[] = L"abca";
    wchar_t *state = 0;
    wchar_t *token;

    if (wcslen_signature(L"wide") != 4) return 1;
    if (wcscpy_signature(buffer, L"ab") != buffer) return 2;
    if (wcscat_signature(buffer, L"cd") != buffer ||
        wcscmp_signature(buffer, L"abcd") != 0) return 3;
    if (wcsncat_signature(buffer, L"EFGH", 2) != buffer ||
        wcscmp_signature(buffer, L"abcdEF") != 0) return 4;

    if (wcsncpy_signature(padded, L"xy", 5) != padded ||
        padded[0] != L'x' || padded[1] != L'y' ||
        padded[2] != 0 || padded[3] != 0 || padded[4] != 0 ||
        padded[5] != L'x') return 5;
    if (wcsncmp_signature(L"abcX", L"abcY", 3) != 0 ||
        wcsncmp_signature(L"abcX", L"abcY", 4) >= 0) return 6;
    if (wcscoll_signature(L"same", L"same") != 0 ||
        wcscoll_signature(L"same", L"samf") >= 0) return 7;

    if (wcsxfrm_signature(transformed, L"wide", 8) != 4 ||
        wcscmp_signature(transformed, L"wide") != 0) return 8;
    if (wcsxfrm_signature(0, L"length", 0) != 6) return 9;

    if (wcschr_signature(buffer, L'c') != buffer + 2 ||
        wcschr_signature(buffer, 0) != buffer + 6 ||
        wcschr_signature(buffer, L'z') != 0) return 10;
    if (wcsrchr_signature(repeated, L'a') != repeated + 3 ||
        wcsrchr_signature(buffer, 0) != buffer + 6 ||
        wcsrchr_signature(buffer, L'z') != 0) return 11;
    if (wcsstr_signature(buffer, L"cdE") != buffer + 2 ||
        wcsstr_signature(buffer, L"") != buffer ||
        wcsstr_signature(buffer, L"missing") != 0) return 12;

    if (wcsspn_signature(L"abc123", L"cba") != 3 ||
        wcscspn_signature(L"abc123", L"0123456789") != 3) return 13;
    if (wcspbrk_signature(buffer, L"xE") != buffer + 4 ||
        wcspbrk_signature(buffer, L"xyz") != 0) return 14;

    token = wcstok_signature(words, L",", &state);
    if (token != words + 1 || wcscmp_signature(token, L"alpha") != 0) return 15;
    token = wcstok_signature(0, L",", &state);
    if (token != words + 8 || wcscmp_signature(token, L"beta") != 0) return 16;
    if (wcstok_signature(0, L",", &state) != 0) return 17;

    if (wmemcpy_signature(copy, memory, 6) != copy ||
        wmemcmp_signature(copy, memory, 6) != 0) return 18;
    if (wmemmove_signature(memory + 1, memory, 4) != memory + 1 ||
        wmemcmp_signature(memory, L"aabcd", 5) != 0) return 19;
    if (wmemset_signature(copy + 2, L'Z', 3) != copy + 2 ||
        copy[1] != L'b' || copy[2] != L'Z' ||
        copy[3] != L'Z' || copy[4] != L'Z') return 20;
    if (wmemchr_signature(copy, L'Z', 6) != copy + 2 ||
        wmemchr_signature(copy, L'Q', 6) != 0 ||
        wmemchr_signature(copy, L'a', 0) != 0) return 21;
    return 0;
}
