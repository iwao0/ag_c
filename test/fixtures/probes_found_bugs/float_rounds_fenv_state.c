/*
 * FLT_ROUNDS is a runtime expression and must follow every successful
 * fesetround() transition while preserving the distinct C encoding.
 */
#include <float.h>
#include <fenv.h>

static int expected_flt_rounds(int mode) {
  if (mode == FE_TOWARDZERO) return 0;
  if (mode == FE_TONEAREST) return 1;
  if (mode == FE_UPWARD) return 2;
  if (mode == FE_DOWNWARD) return 3;
  return -1;
}

static int check_mode(int mode, int code) {
  if (fesetround(mode) != 0) return code;
  if (fegetround() != mode) return code + 1;
  if (FLT_ROUNDS != expected_flt_rounds(mode)) return code + 2;
  return 0;
}

int main(void) {
  fenv_t original;
  int result;

  if (fegetenv(&original) != 0) return 1;
  result = check_mode(FE_TONEAREST, 10);
  if (!result) result = check_mode(FE_TOWARDZERO, 20);
  if (!result) result = check_mode(FE_UPWARD, 30);
  if (!result) result = check_mode(FE_DOWNWARD, 40);
  if (fesetenv(&original) != 0 && !result) result = 2;
  return result;
}
