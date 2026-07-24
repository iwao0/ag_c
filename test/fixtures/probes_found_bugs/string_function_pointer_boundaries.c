// string.h signatures and indirect-call behavior across all API categories.
// Expected: exit=0
#include <stddef.h>
#include <string.h>

static void *(*memcpy_fn)(void *, const void *, size_t) = memcpy;
static void *(*memmove_fn)(void *, const void *, size_t) = memmove;
static char *(*strcpy_fn)(char *, const char *) = strcpy;
static char *(*strncpy_fn)(char *, const char *, size_t) = strncpy;
static char *(*strcat_fn)(char *, const char *) = strcat;
static char *(*strncat_fn)(char *, const char *, size_t) = strncat;
static int (*memcmp_fn)(const void *, const void *, size_t) = memcmp;
static int (*strcmp_fn)(const char *, const char *) = strcmp;
static int (*strncmp_fn)(const char *, const char *, size_t) = strncmp;
static int (*strcoll_fn)(const char *, const char *) = strcoll;
static size_t (*strxfrm_fn)(char *, const char *, size_t) = strxfrm;
static void *(*memchr_fn)(const void *, int, size_t) = memchr;
static char *(*strchr_fn)(const char *, int) = strchr;
static char *(*strrchr_fn)(const char *, int) = strrchr;
static size_t (*strspn_fn)(const char *, const char *) = strspn;
static size_t (*strcspn_fn)(const char *, const char *) = strcspn;
static char *(*strpbrk_fn)(const char *, const char *) = strpbrk;
static char *(*strstr_fn)(const char *, const char *) = strstr;
static char *(*strtok_fn)(char *, const char *) = strtok;
static void *(*memset_fn)(void *, int, size_t) = memset;
static size_t (*strlen_fn)(const char *) = strlen;
static char *(*strerror_fn)(int) = strerror;

int main(void) {
    char copied[24] = {0};
    char moved[8] = "abcdef";
    char transformed[8] = {0};
    char tokens[] = "ab,cd";
    const char source[] = "abc";
    const char repeated[] = "abca";
    const char haystack[] = "bananarama";
    char *token;

    if (memcpy_fn(copied, source, sizeof(source)) != copied) return 1;
    if (strcmp_fn(copied, source) != 0) return 2;
    if (memmove_fn(moved + 1, moved, 6) != moved + 1 ||
        memcmp_fn(moved, "aabcdef", 7) != 0) return 3;

    if (strcpy_fn(copied, "xy") != copied) return 4;
    if (strncpy_fn(copied + 2, "z", 3) != copied + 2 ||
        copied[2] != 'z' || copied[3] != '\0' || copied[4] != '\0') return 5;
    copied[2] = '\0';
    if (strcat_fn(copied, "z") != copied || strcmp_fn(copied, "xyz") != 0) return 6;
    if (strncat_fn(copied, "1234", 2) != copied ||
        strncmp_fn(copied, "xyz12", 6) != 0) return 7;

    if (strcoll_fn("abc", "abd") >= 0) return 8;
    if (strxfrm_fn(transformed, "abc", sizeof(transformed)) != 3 ||
        strcmp_fn(transformed, "abc") != 0) return 9;

    if (memchr_fn(repeated, 'b', 4) != repeated + 1) return 10;
    if (strchr_fn(repeated, 'a') != repeated) return 11;
    if (strrchr_fn(repeated, 'a') != repeated + 3) return 12;
    if (strspn_fn("aabbc", "ab") != 4) return 13;
    if (strcspn_fn("aabbc", "c") != 4) return 14;
    if (strpbrk_fn(repeated, "cx") != repeated + 2) return 15;
    if (strstr_fn(haystack, "ana") != haystack + 1) return 16;

    token = strtok_fn(tokens, ",");
    if (!token || strcmp_fn(token, "ab") != 0) return 17;
    token = strtok_fn(0, ",");
    if (!token || strcmp_fn(token, "cd") != 0 || strtok_fn(0, ",") != 0) return 18;

    if (memset_fn(copied, 'Q', 3) != copied ||
        copied[0] != 'Q' || copied[1] != 'Q' || copied[2] != 'Q') return 19;
    copied[3] = '\0';
    if (strlen_fn(copied) != 3) return 20;
    if (!strerror_fn(0) || strerror_fn(0)[0] == '\0') return 21;
    return 0;
}
