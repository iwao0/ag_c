// locale.h minimal runtime calls
// Expected: exit=0
#include <locale.h>
#include <string.h>

int main(void) {
    char *name = setlocale(LC_ALL, 0);
    struct lconv *lc = localeconv();
    if (!name) return 1;
    if (!lc) return 2;
    if (strcmp(name, "C") != 0) return 3;
    if (!lc->decimal_point) return 4;
    if (strcmp(lc->decimal_point, ".") != 0) return 5;
    return 0;
}
