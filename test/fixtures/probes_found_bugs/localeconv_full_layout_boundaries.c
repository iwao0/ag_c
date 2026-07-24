// locale.h full lconv layout and C-locale field boundaries.
// Expected: exit=0
#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <string.h>

static struct lconv *(*localeconv_signature)(void) = localeconv;
static char *(*setlocale_signature)(int, const char *) = setlocale;

static int is_empty(const char *value) {
    return value && strcmp(value, "") == 0;
}

int main(void) {
    struct lconv *value;
    char *locale_name;

#ifdef __wasm32__
    if (sizeof(struct lconv) != 56 ||
        offsetof(struct lconv, negative_sign) != 36 ||
        offsetof(struct lconv, int_frac_digits) != 40 ||
        offsetof(struct lconv, int_n_sign_posn) != 53)
        return 1;
#else
    if (sizeof(struct lconv) != 96 ||
        offsetof(struct lconv, negative_sign) != 72 ||
        offsetof(struct lconv, int_frac_digits) != 80 ||
        offsetof(struct lconv, int_n_sign_posn) != 93)
        return 2;
#endif

    locale_name = setlocale_signature(LC_ALL, "C");
    if (!locale_name || strcmp(locale_name, "C") != 0)
        return 3;
    value = localeconv_signature();
    if (!value || !value->decimal_point ||
        strcmp(value->decimal_point, ".") != 0)
        return 4;
    if (!is_empty(value->thousands_sep) ||
        !is_empty(value->grouping) ||
        !is_empty(value->int_curr_symbol) ||
        !is_empty(value->currency_symbol) ||
        !is_empty(value->mon_decimal_point) ||
        !is_empty(value->mon_thousands_sep) ||
        !is_empty(value->mon_grouping) ||
        !is_empty(value->positive_sign) ||
        !is_empty(value->negative_sign))
        return 5;
    if (value->int_frac_digits != CHAR_MAX ||
        value->frac_digits != CHAR_MAX ||
        value->p_cs_precedes != CHAR_MAX ||
        value->p_sep_by_space != CHAR_MAX ||
        value->n_cs_precedes != CHAR_MAX ||
        value->n_sep_by_space != CHAR_MAX ||
        value->p_sign_posn != CHAR_MAX ||
        value->n_sign_posn != CHAR_MAX ||
        value->int_p_cs_precedes != CHAR_MAX ||
        value->int_n_cs_precedes != CHAR_MAX ||
        value->int_p_sep_by_space != CHAR_MAX ||
        value->int_n_sep_by_space != CHAR_MAX ||
        value->int_p_sign_posn != CHAR_MAX ||
        value->int_n_sign_posn != CHAR_MAX)
        return 6;
    return 0;
}
