#include <assert.h>

int shared_values[];

static int read_shared(int index) {
  extern int shared_values[];
  return shared_values[index];
}

static int read_after_shadow(void) {
  int result = 0;
  {
    int shared_values[2] = {100, 200};
    result += shared_values[1];
  }
  {
    extern int shared_values[];
    result += shared_values[2];
  }
  return result;
}

int shared_values[3] = {4, 5, 6};

int main(void) {
  assert(read_shared(1) == 5);
  assert(read_after_shadow() == 206);
  shared_values[2] = 42;
  assert(read_shared(2) == 42);
  return 0;
}
