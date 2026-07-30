// Cross-TU unspecified function pointers with direct, indirect, and complex
// return ABIs.
// Expected with unprototyped_funcptr_return_xtu_other.c: exit=42

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

typedef struct small_result small_callback_t();
typedef struct wide_result wide_callback_t();
typedef double _Complex complex_callback_t();

struct small_result make_small_result();
struct wide_result make_wide_result();
double _Complex make_complex_result();

static small_callback_t *small_callbacks[] = {
    make_small_result,
    make_small_result,
};
static wide_callback_t *wide_callback = make_wide_result;

struct callback_holder {
  complex_callback_t *complex_callback;
};

static struct callback_holder callbacks = {
    make_complex_result,
};

static int selector_count;

static wide_callback_t *select_wide_callback(void) {
  selector_count++;
  return wide_callback;
}

int main(void) {
  signed char seed = 7;
  float scale = 2.5f;
  struct small_result small = small_callbacks[1](seed, scale);
  struct wide_result wide = select_wide_callback()(seed, scale);
  double _Complex complex_value =
      callbacks.complex_callback(seed, scale);

  if (small.value != 9)
    return 1;
  if (wide.first != 7 || wide.second != 25 || wide.total != 32)
    return 2;
  if (creal(complex_value) != 7.0 || cimag(complex_value) != 2.5)
    return 3;
  if (selector_count != 1)
    return 4;
  return 42;
}
