// fenv.h minimal runtime calls
// Expected: exit=0
#include <fenv.h>

int main(void) {
    fenv_t env = {0, 0};
    fexcept_t flag = 0;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 1;
    if (fegetexceptflag(&flag, FE_INVALID) != 0) return 2;
    if (fesetexceptflag(&flag, FE_INVALID) != 0) return 3;
    if (fetestexcept(FE_INVALID) != 0) return 4;
    if (feraiseexcept(FE_INVALID) != 0) return 5;
    if ((fetestexcept(FE_INVALID) & FE_INVALID) == 0) return 6;
    if (fegetexceptflag(&flag, FE_INVALID) != 0) return 7;
    if (feclearexcept(FE_INVALID) != 0) return 8;
    if (fetestexcept(FE_INVALID) != 0) return 9;
    if (fesetexceptflag(&flag, FE_INVALID) != 0) return 10;
    if ((fetestexcept(FE_INVALID) & FE_INVALID) == 0) return 11;
    int round = fegetround();
    if (fesetround(round) != 0) return 12;
    if (fegetenv(&env) != 0) return 13;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 14;
    if (fetestexcept(FE_ALL_EXCEPT) != 0) return 15;
    if (fesetenv(&env) != 0) return 16;
    if ((fetestexcept(FE_INVALID) & FE_INVALID) == 0) return 17;
    if (feraiseexcept(FE_INEXACT) != 0) return 18;
    if (feholdexcept(&env) != 0) return 19;
    if (fetestexcept(FE_ALL_EXCEPT) != 0) return 20;
    if (fesetenv(&env) != 0) return 21;
    if ((fetestexcept(FE_INVALID | FE_INEXACT) & (FE_INVALID | FE_INEXACT)) !=
        (FE_INVALID | FE_INEXACT)) return 22;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 23;
    if (feraiseexcept(FE_UNDERFLOW) != 0) return 24;
    if (feupdateenv(&env) != 0) return 25;
    if ((fetestexcept(FE_INVALID | FE_INEXACT | FE_UNDERFLOW) &
         (FE_INVALID | FE_INEXACT | FE_UNDERFLOW)) !=
        (FE_INVALID | FE_INEXACT | FE_UNDERFLOW)) return 26;
    return 0;
}
