#include "target_info.h"

static ag_target_info_t default_target = {8};

ag_target_info_t ag_target_info_host(void) {
  return (ag_target_info_t){8};
}

ag_target_info_t ag_target_info_wasm32(void) {
  return (ag_target_info_t){4};
}

int ag_target_info_pointer_size(const ag_target_info_t *target) {
  return target && target->pointer_size == 4 ? 4 : 8;
}

int ag_target_pointer_size(void) {
  return ag_target_info_pointer_size(&default_target);
}

void ag_target_set_pointer_size(int size) {
  default_target.pointer_size = size == 4 ? 4 : 8;
}
