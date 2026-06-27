#include <assert.h>

#define VALUE 1
#pragma push_macro("VALUE")
#undef VALUE
#define VALUE 2
#pragma pop_macro("VALUE")

int global_range[5] = {[1 ... 3] = 7};

struct zero_tail {
  int head;
  int tail[0];
};

int main(void) {
  int local_range[5] = {[2 ... 4] = 9};

  assert(VALUE == 2);
  assert(sizeof(struct zero_tail) == sizeof(int));

  assert(global_range[0] == 0);
  assert(global_range[1] == 7);
  assert(global_range[2] == 0);
  assert(global_range[3] == 0);
  assert(global_range[4] == 0);

  assert(local_range[0] == 0);
  assert(local_range[1] == 0);
  assert(local_range[2] == 9);
  assert(local_range[3] == 0);
  assert(local_range[4] == 0);

  return 0;
}
