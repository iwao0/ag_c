#include <assert.h>

struct pointer_pair {
  int *first;
  int *second;
};

static int global_value = 17;
static const int *file_const_value = &(const int){19};
static struct pointer_pair file_pointers = {0, &global_value};

int main(void) {
  static int *block_static_pointer = &global_value;
  assert(((int){1} = 2) == 2);
  assert(((int){3} += 4) == 7);
  assert(++(int){8} == 9);
  assert((int){9}++ == 9);

  int *automatic_value = &(int){12};
  assert(*automatic_value == 12);
  assert(*file_const_value == 19);
  assert(file_pointers.first == 0);
  assert(file_pointers.second == &global_value);
  assert(block_static_pointer == &global_value);

  int side_effect = 0;
  assert(sizeof((int){side_effect++}) == sizeof(int));
  assert(side_effect == 0);
  return 0;
}
