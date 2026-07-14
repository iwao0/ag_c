#include "config_runtime.h"
#include "runtime_context.h"

bool ps_get_enable_size_compatible_nonscalar_cast(void) {
  return ps_parser_runtime_context_active()
      ->enable_size_compatible_nonscalar_cast;
}

void ps_set_enable_size_compatible_nonscalar_cast(bool enable) {
  ps_parser_runtime_context_active()
      ->enable_size_compatible_nonscalar_cast = enable;
}

bool ps_get_enable_union_scalar_pointer_cast(void) {
  return ps_parser_runtime_context_active()
      ->enable_union_scalar_pointer_cast;
}

void ps_set_enable_union_scalar_pointer_cast(bool enable) {
  ps_parser_runtime_context_active()
      ->enable_union_scalar_pointer_cast = enable;
}

bool ps_get_enable_union_array_member_nonbrace_init(void) {
  return ps_parser_runtime_context_active()
      ->enable_union_array_member_nonbrace_init;
}

void ps_set_enable_union_array_member_nonbrace_init(bool enable) {
  ps_parser_runtime_context_active()
      ->enable_union_array_member_nonbrace_init = enable;
}

bool ps_get_enable_struct_scalar_pointer_cast(void) {
  return ps_parser_runtime_context_active()
      ->enable_struct_scalar_pointer_cast;
}

void ps_set_enable_struct_scalar_pointer_cast(bool enable) {
  ps_parser_runtime_context_active()
      ->enable_struct_scalar_pointer_cast = enable;
}
