#include <assert.h>

struct flags_and_pointer {
  unsigned char first : 1;
  unsigned char second : 1;
  int *pointer;
};

int main(void) {
  int value = 42;
  struct flags_and_pointer state = {0, 0, &value};
  state.first = 1;
  state.second = 1;
  assert(state.first == 1);
  assert(state.second == 1);
  assert(state.pointer == &value);
  assert(*state.pointer == 42);
  return 0;
}
