// Wasm-only fenv default environment behavior.
// Expected: exit=0
#include <fenv.h>

int main(void) {
    if (feraiseexcept(FE_INVALID | FE_INEXACT) != 0) return 1;
    if ((fetestexcept(FE_ALL_EXCEPT) & (FE_INVALID | FE_INEXACT)) !=
        (FE_INVALID | FE_INEXACT)) return 2;
    if (fesetround(FE_DOWNWARD) != 0) return 3;
    if (fegetround() != FE_DOWNWARD) return 4;
    if (fesetenv(FE_DFL_ENV) != 0) return 5;
    if (fetestexcept(FE_ALL_EXCEPT) != 0) return 6;
    if (fegetround() != FE_TONEAREST) return 7;
    return 0;
}
