#include "target_info.h"

static int target_pointer_size = 8;

int ag_target_pointer_size(void) {
  return target_pointer_size;
}

void ag_target_set_pointer_size(int size) {
  target_pointer_size = size == 4 ? 4 : 8;
}
