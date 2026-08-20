#include <assert.h>
#include <stdint.h>

static int direct_vla_alignment(int count) {
  int effect = 0;
  _Alignas(int[count]) int direct_array_type = 17;
  _Alignas(int (*)[count]) int pointer_to_vla_type = 19;
  _Alignas(int[(effect++, count)]) int unevaluated_bound = 23;

  assert((uintptr_t)&direct_array_type % _Alignof(int) == 0);
  assert((uintptr_t)&pointer_to_vla_type % _Alignof(int *) == 0);
  assert((uintptr_t)&unevaluated_bound % _Alignof(int) == 0);
  assert(effect == 0);
  return direct_array_type + pointer_to_vla_type + unevaluated_bound;
}

int main(void) {
  assert(direct_vla_alignment(3) == 59);
  return 0;
}
