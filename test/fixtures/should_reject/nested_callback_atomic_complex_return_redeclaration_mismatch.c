#include <complex.h>

typedef float complex plain_callback_function(void);
typedef _Atomic(float complex) atomic_callback_function(void);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(float complex) value = callback();
  return sizeof value;
}

int main(void) {
  return 0;
}
