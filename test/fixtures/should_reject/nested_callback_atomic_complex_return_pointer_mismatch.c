#include <complex.h>

typedef double complex plain_callback_function(void);
typedef _Atomic(double complex) atomic_callback_function(void);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(double complex) value = callback();
  return sizeof value;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}
