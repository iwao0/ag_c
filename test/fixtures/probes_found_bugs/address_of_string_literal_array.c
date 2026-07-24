#include <assert.h>

static char (*global_pointer)[4] = &"abc";

int main(void) {
  char (*local_pointer)[4] = &"abc";

  assert(_Generic(&"abc", char (*)[4]: 1, default: 0));
  assert(sizeof *local_pointer == 4);
  assert((*local_pointer)[0] == 'a');
  assert((*local_pointer)[2] == 'c');
  assert((*global_pointer)[1] == 'b');
  assert(&"abc"[2] != 0);
  return 0;
}
