// Wasm-only fenv default environment behavior.
// Expected: exit=0
#include <fenv.h>

int main(void) {
    fenv_t invalid_env = {0x12345678, 0};
    if (feraiseexcept(FE_INVALID | FE_INEXACT) != 0) return 1;
    if ((fetestexcept(FE_ALL_EXCEPT) & (FE_INVALID | FE_INEXACT)) !=
        (FE_INVALID | FE_INEXACT)) return 2;
    if (fesetround(FE_DOWNWARD) != 0) return 3;
    if (fegetround() != FE_DOWNWARD) return 4;
    if (fesetround(0x12345678) == 0) return 5;
    if (fegetround() != FE_DOWNWARD) return 6;
    if (fesetenv(&invalid_env) == 0) return 7;
    if (fegetround() != FE_DOWNWARD) return 8;
    if (fesetenv(FE_DFL_ENV) != 0) return 9;
    if (fetestexcept(FE_ALL_EXCEPT) != 0) return 10;
    if (fegetround() != FE_TONEAREST) return 11;
    return 0;
}
