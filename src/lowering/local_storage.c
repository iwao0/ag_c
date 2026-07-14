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

lvar_t *ps_decl_register_lvar_typed_align(
    char *name, int len, int size, int align, const psx_type_t *type) {
  if (!type) return NULL;
  int offset = local_storage_allocate(size, align);
  return ps_local_registry_create_storage_object(
      name, len, offset, size, align, type);
}

void ps_decl_reset_locals(void) {
  ps_local_registry_reset();
  local_storage_reset();
}

void ps_decl_reserve_variadic_regs(void) {
  local_storage_reserve_prefix(64);
}
