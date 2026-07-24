// fenv.h signatures and state transitions through indirect calls.
// Expected: exit=0
#include <fenv.h>

static int (*feclearexcept_signature)(int) = feclearexcept;
static int (*fegetexceptflag_signature)(fexcept_t *, int) =
    fegetexceptflag;
static int (*feraiseexcept_signature)(int) = feraiseexcept;
static int (*fesetexceptflag_signature)(const fexcept_t *, int) =
    fesetexceptflag;
static int (*fetestexcept_signature)(int) = fetestexcept;
static int (*fegetround_signature)(void) = fegetround;
static int (*fesetround_signature)(int) = fesetround;
static int (*fegetenv_signature)(fenv_t *) = fegetenv;
static int (*feholdexcept_signature)(fenv_t *) = feholdexcept;
static int (*fesetenv_signature)(const fenv_t *) = fesetenv;
static int (*feupdateenv_signature)(const fenv_t *) = feupdateenv;

static int restore_and_fail(const fenv_t *original, int code) {
    fesetenv_signature(original);
    return code;
}

int main(void) {
    fenv_t original = {0, 0};
    fenv_t held = {0, 0};
    fexcept_t flags = 0;
    int original_round;
    int expected;

    if (fegetenv_signature(&original) != 0) return 1;
    original_round = fegetround_signature();
    if (fesetround_signature(original_round) != 0)
        return restore_and_fail(&original, 2);

    if (feclearexcept_signature(FE_ALL_EXCEPT) != 0 ||
        fetestexcept_signature(FE_ALL_EXCEPT) != 0)
        return restore_and_fail(&original, 3);
    expected = FE_INVALID;
    if (feraiseexcept_signature(expected) != 0 ||
        fetestexcept_signature(FE_ALL_EXCEPT) != expected)
        return restore_and_fail(&original, 4);
    if (fegetexceptflag_signature(&flags, FE_ALL_EXCEPT) != 0)
        return restore_and_fail(&original, 5);

    if (feclearexcept_signature(FE_INVALID) != 0 ||
        fetestexcept_signature(FE_ALL_EXCEPT) != 0)
        return restore_and_fail(&original, 6);
    if (fesetexceptflag_signature(&flags, FE_INVALID) != 0 ||
        fetestexcept_signature(FE_ALL_EXCEPT) != expected)
        return restore_and_fail(&original, 7);

    flags = FE_UNDERFLOW;
    if (fesetexceptflag_signature(
            &flags, FE_INVALID | FE_UNDERFLOW) != 0)
        return restore_and_fail(&original, 8);
    expected = FE_UNDERFLOW;
    if (fetestexcept_signature(FE_ALL_EXCEPT) != expected)
        return restore_and_fail(&original, 9);

    if (feholdexcept_signature(&held) != 0 ||
        fetestexcept_signature(FE_ALL_EXCEPT) != 0)
        return restore_and_fail(&original, 10);
    if (feraiseexcept_signature(FE_INEXACT) != 0 ||
        feupdateenv_signature(&held) != 0)
        return restore_and_fail(&original, 11);
    expected |= FE_INEXACT;
    if (fetestexcept_signature(FE_ALL_EXCEPT) != expected)
        return restore_and_fail(&original, 12);

    if (fesetround_signature(FE_UPWARD) != 0 ||
        fegetround_signature() != FE_UPWARD ||
        fesetround_signature(FE_DOWNWARD) != 0 ||
        fegetround_signature() != FE_DOWNWARD ||
        fesetround_signature(FE_TOWARDZERO) != 0 ||
        fegetround_signature() != FE_TOWARDZERO ||
        fesetround_signature(FE_TONEAREST) != 0 ||
        fegetround_signature() != FE_TONEAREST)
        return restore_and_fail(&original, 13);

    if (fesetenv_signature(FE_DFL_ENV) != 0 ||
        fegetround_signature() != FE_TONEAREST ||
        fetestexcept_signature(FE_ALL_EXCEPT) != 0)
        return restore_and_fail(&original, 14);
    if (fesetenv_signature(&original) != 0) return 15;
    return 0;
}
