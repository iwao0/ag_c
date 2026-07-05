// WAT standalone math.h classification/comparison stubs
// Expected: exit=0
#include <math.h>

int main(void) {
    double z = 0.0;
    double nanv = z / z;
    double infv = 1.0 / z;
    double nzero = -0.0;
    double sub = 1.0e-320;

    if (!isnan(nanv)) return 1;
    if (isnan(1.0)) return 2;
    if (!isinf(infv) || isinf(42.0)) return 3;
    if (!isfinite(42.0) || isfinite(infv) || isfinite(nanv)) return 4;
    if (!isnormal(42.0) || isnormal(0.0) || isnormal(infv) || isnormal(nanv)) return 5;
    if (!signbit(-1.0) || !signbit(nzero) || signbit(0.0) || signbit(1.0)) return 6;

    if (fpclassify(nanv) != FP_NAN) return 7;
    if (fpclassify(infv) != FP_INFINITE) return 8;
    if (fpclassify(0.0) != FP_ZERO) return 9;
    if (fpclassify(sub) != FP_SUBNORMAL) return 10;
    if (fpclassify(1.0) != FP_NORMAL) return 11;

    if (!isgreater(3.0, 2.0) || isgreater(2.0, 3.0)) return 12;
    if (!isgreaterequal(3.0, 3.0) || isgreaterequal(2.0, 3.0)) return 13;
    if (!isless(2.0, 3.0) || isless(3.0, 2.0)) return 14;
    if (!islessequal(3.0, 3.0) || islessequal(3.0, 2.0)) return 15;
    if (!islessgreater(2.0, 3.0) || islessgreater(3.0, 3.0)) return 16;
    if (!isunordered(nanv, 1.0) || isunordered(1.0, 2.0)) return 17;
    if (isgreater(nanv, 1.0) || isless(1.0, nanv) || islessgreater(nanv, nanv)) return 18;

    return 0;
}
