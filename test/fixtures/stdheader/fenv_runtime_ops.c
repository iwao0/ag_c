// fenv.h minimal runtime calls
// Expected: exit=0
#include <fenv.h>

int main(void) {
    fenv_t env = {0, 0};
    fexcept_t flag = 0;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 1;
    if (fegetexceptflag(&flag, FE_INVALID) != 0) return 2;
    if (fesetexceptflag(&flag, FE_INVALID) != 0) return 3;
    if (feraiseexcept(FE_INVALID) != 0) return 4;
    if ((fetestexcept(FE_INVALID) & FE_INVALID) == 0) return 5;
    int round = fegetround();
    if (fesetround(round) != 0) return 6;
    if (fegetenv(&env) != 0) return 7;
    if (feholdexcept(&env) != 0) return 8;
    if (fesetenv(&env) != 0) return 9;
    if (feupdateenv(&env) != 0) return 10;
    return 0;
}
