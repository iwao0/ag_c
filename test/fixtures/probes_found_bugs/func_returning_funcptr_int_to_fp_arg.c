/*
 * Function-returned function pointer signatures must preserve parameter ABI
 * metadata. `pick()(3)` is an indirect call through a returned `double (*)(double)`,
 * so the integer literal has to be promoted to double before the call.
 */
#include <assert.h>

double add_half(double x) { return x + 0.5; }

double (*pick(void))(double) { return add_half; }

int main(void) {
  assert(pick()(3) == 3.5);
  return 0;
}
