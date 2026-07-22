#include <assert.h>

int main(void) {
  int value = 42;
  int choose_pointer = 1;
  void *pointer = &value;
  const void *const_pointer = choose_pointer ? (const void *)pointer : 0;

  assert(const_pointer == &value);
  assert(_Generic((choose_pointer ? pointer : 0), void *: 1, default: 0));

  int *selected = choose_pointer ? pointer : 0;
  assert(selected == &value && *selected == 42);
  choose_pointer = 0;
  selected = choose_pointer ? pointer : 0;
  assert(selected == 0);
  selected = choose_pointer ? 0 : pointer;
  assert(selected == &value);
  return 0;
}
