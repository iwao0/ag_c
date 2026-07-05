// WAT standalone math.h nan/nanf/nanl stubs
// Expected: exit=0
#include <math.h>

typedef double (*nan_fn_t)(const char *);

static int check_nan_fn(nan_fn_t fn) {
    double v = fn("payload");
    return isnan(v) ? 0 : 1;
}

int main(void) {
    double d = nan("");
    float f = nanf("ignored");
    long double ld = nanl("123");
    nan_fn_t pnan = nan;

    if (!isnan(d)) return 1;
    if (!isnan(f)) return 2;
    if (!isnan(ld)) return 3;
    if (check_nan_fn(pnan) != 0) return 4;

    return 0;
}
