// Cross-TU prototype definitions paired with
// unprototyped_funcptr_return_xtu_main.c.

#include <complex.h>

#ifndef AGC_UNPROTOTYPED_FUNCPTR_RETURN_XTU_TYPES
#define AGC_UNPROTOTYPED_FUNCPTR_RETURN_XTU_TYPES
struct small_result {
  int value;
};

struct wide_result {
  long long first;
  long long second;
  long long total;
};
#endif

struct small_result make_small_result(int seed, double scale) {
  struct small_result result = {
      seed + (int)scale,
  };
  return result;
}

struct wide_result make_wide_result(int seed, double scale) {
  struct wide_result result = {
      seed,
      (long long)(scale * 10.0),
      seed + (long long)(scale * 10.0),
  };
  return result;
}

double _Complex make_complex_result(int seed, double scale) {
  return (double)seed + scale * I;
}
