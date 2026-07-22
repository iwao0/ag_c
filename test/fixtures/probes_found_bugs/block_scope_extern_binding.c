#include <assert.h>

int shared_value = 7;

static int read_with_separate_scopes(void) {
  int result = 0;
  {
    extern int shared_value;
    result += shared_value;
  }
  {
    int shared_value = 100;
    result += shared_value;
  }
  {
    extern int shared_value;
    result += shared_value;
  }
  return result;
}

int main(void) {
  assert(read_with_separate_scopes() == 114);
  assert(shared_value == 7);
  return 0;
}
