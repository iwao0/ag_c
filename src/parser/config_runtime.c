#include "config_runtime.h"

static bool enable_size_compatible_nonscalar_cast = true;

bool ps_get_enable_size_compatible_nonscalar_cast(void) {
  return enable_size_compatible_nonscalar_cast;
}

void ps_set_enable_size_compatible_nonscalar_cast(bool enable) {
  enable_size_compatible_nonscalar_cast = enable;
}
