// Wasm standalone libc stubs through function pointers.
// Expected: exit=0
#include <fenv.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *(*move_fn_t)(void *, const void *, size_t);

static move_fn_t g_move = memmove;

static int cmp_ints(const void *a, const void *b) {
    int av = *(const int *)a;
    int bv = *(const int *)b;
    return (av > bv) - (av < bv);
}

static move_fn_t choose_move(move_fn_t fn) {
    return fn;
}

static int check_move_fn(move_fn_t fn) {
    char tmp[8] = "xxxxx";
    if (fn(tmp, "def", 4) != tmp) return 1;
    if (strcmp(tmp, "def") != 0) return 2;
    return 0;
}

int main(void) {
    char dst[8] = "xxxxx";
    const char src[] = "abc";
    move_fn_t pmemmove = memmove;
    char *(*pstrerror)(int) = strerror;
    int (*pfputs)(const char *, FILE *) = fputs;
    int (*pfputc)(int, FILE *) = fputc;
    time_t (*ptime)(time_t *) = time;
    double (*pdifftime)(time_t, time_t) = difftime;
    int (*pfesetround)(int) = fesetround;
    int (*pfegetround)(void) = fegetround;
    char *(*psetlocale)(int, const char *) = setlocale;
    struct lconv *(*plocaleconv)(void) = localeconv;
    void (*psrand)(unsigned int) = srand;
    int (*prand)(void) = rand;
    void (*pqsort)(void *, size_t, size_t, int (*)(const void *, const void *)) = qsort;
    FILE *stream = (FILE *)1;
    time_t t = 123;
    char *err5;
    char *err0;
    char *loc;
    struct lconv *lc;
    int values[3] = {3, 1, 2};

    if (pmemmove(dst, src, 4) != dst || strcmp(dst, "abc") != 0) return 1;
    err5 = pstrerror(5);
    err0 = pstrerror(0);
    if (err5[0] != 'e' || strcmp(err0, err5) == 0) return 2;
    if (pfputs("xy", stream) != 2 || pfputs("xy", 0) != EOF) return 3;
    if (pfputc('Z', stream) != 'Z' || pfputc('Z', 0) != EOF) return 4;
    if (ptime(&t) != 0 || t != 0) return 5;
    if ((int)pdifftime(100, 58) != 42) return 6;
    if (pfesetround(FE_UPWARD) != 0 || pfegetround() != FE_UPWARD) return 7;
    loc = psetlocale(LC_ALL, "C");
    if (loc[0] != 'C') return 8;
    lc = plocaleconv();
    if (!lc || !lc->decimal_point || lc->decimal_point[0] != '.') return 9;
    psrand(1);
    if (prand() != 16838) return 10;
    pqsort(values, 3, sizeof(values[0]), cmp_ints);
    if (values[0] != 1 || values[1] != 2 || values[2] != 3) return 11;
    if (check_move_fn(g_move) != 0) return 12;
    if (check_move_fn(choose_move(pmemmove)) != 0) return 13;
    return 0;
}
