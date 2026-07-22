#include <assert.h>

/* ag_c extension: C11 atomic initialization remained underspecified, and
 * implementations disagree on brace-enclosed atomic initializers. Keep the
 * existing qualifier-based behavior stable in non-strict mode. */
_Atomic int file_value = {3};
static int target = 8;
_Atomic(int *) file_pointer = {&target};

int main(void) {
  _Atomic int local_value = {4};
  _Atomic(int *) local_pointer = {&target};

  assert(file_value == 3);
  assert(local_value == 4);
  assert((_Atomic(int)){5} == 5);
  assert(++(_Atomic(int)){6} == 7);
  assert(*file_pointer == 8);
  assert(*local_pointer == 8);
  return 0;
}
