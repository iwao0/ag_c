#include "local_storage.h"

#include "frame_layout.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"

static frame_layout_t current_layout;

void local_storage_reset(void) {
  frame_layout_reset(&current_layout);
}

void local_storage_reserve_prefix(int bytes) {
  frame_layout_reserve_prefix(&current_layout, bytes);
}

int local_storage_allocate(int size, int align) {
  return frame_layout_allocate(&current_layout, size, align);
}

static psx_type_t *default_storage_type(
    int size, int elem_size, int is_array) {
  int scalar_size = elem_size > 0 ? elem_size : size;
  if (scalar_size <= 0) scalar_size = 1;
  psx_type_t *scalar = ps_type_new_integer(TK_EOF, scalar_size, 0);
  if (!is_array) return scalar;
  int array_len = size > 0 && size % scalar_size == 0
                      ? size / scalar_size
                      : 0;
  return ps_type_new_array(scalar, array_len, size, 0);
}

lvar_t *ps_decl_register_lvar_typed_align(
    char *name, int len, int size, int align, const psx_type_t *type) {
  if (!type) return NULL;
  int offset = local_storage_allocate(size, align);
  return ps_local_registry_create_storage_object(
      name, len, offset, size, align, type);
}

lvar_t *ps_decl_register_lvar_sized_align(
    char *name, int len, int size, int elem_size,
    int is_array, int align) {
  int offset = local_storage_allocate(size, align);
  psx_type_t *type = default_storage_type(size, elem_size, is_array);
  return ps_local_registry_create_storage_object(
      name, len, offset, size, align, type);
}

lvar_t *ps_decl_register_lvar_sized(
    char *name, int len, int size, int elem_size, int is_array) {
  return ps_decl_register_lvar_sized_align(
      name, len, size, elem_size, is_array, 0);
}

lvar_t *ps_decl_register_lvar(char *name, int len) {
  return ps_decl_register_lvar_sized(name, len, 8, 8, 0);
}

void ps_decl_reset_locals(void) {
  ps_local_registry_reset();
  local_storage_reset();
}

void ps_decl_reserve_variadic_regs(void) {
  local_storage_reserve_prefix(64);
}
