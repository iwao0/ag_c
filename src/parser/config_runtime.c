#include "config_runtime.h"

static bool enable_size_compatible_nonscalar_cast = true;
static bool enable_union_scalar_pointer_cast = true;
static bool enable_union_array_member_nonbrace_init = true;
static bool enable_struct_scalar_pointer_cast = true;

bool ps_get_enable_size_compatible_nonscalar_cast(void) {
  return enable_size_compatible_nonscalar_cast;
}

void ps_set_enable_size_compatible_nonscalar_cast(bool enable) {
  enable_size_compatible_nonscalar_cast = enable;
}

bool ps_get_enable_union_scalar_pointer_cast(void) {
  return enable_union_scalar_pointer_cast;
}

void ps_set_enable_union_scalar_pointer_cast(bool enable) {
  enable_union_scalar_pointer_cast = enable;
}

bool ps_get_enable_union_array_member_nonbrace_init(void) {
  return enable_union_array_member_nonbrace_init;
}

void ps_set_enable_union_array_member_nonbrace_init(bool enable) {
  enable_union_array_member_nonbrace_init = enable;
}

bool ps_get_enable_struct_scalar_pointer_cast(void) {
  return enable_struct_scalar_pointer_cast;
}

void ps_set_enable_struct_scalar_pointer_cast(bool enable) {
  enable_struct_scalar_pointer_cast = enable;
}
