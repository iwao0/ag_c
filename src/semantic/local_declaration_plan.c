#include "local_declaration_plan.h"

int psx_plan_complete_array_storage(
    const psx_type_t *type, psx_complete_array_storage_plan_t *out) {
  if (!type || !out || type->kind != PSX_TYPE_ARRAY || type->is_vla)
    return 0;

  const psx_type_t *leaf = type;
  while (leaf && leaf->kind == PSX_TYPE_ARRAY) {
    if (leaf->is_vla || leaf->array_len <= 0 || ps_type_sizeof(leaf) <= 0)
      return 0;
    leaf = leaf->base;
  }
  int storage_size = ps_type_sizeof(type);
  int scalar_size = ps_type_sizeof(leaf);
  if (storage_size <= 0 || scalar_size <= 0) return 0;

  int alignment = type->align;
  if (alignment <= 0 && leaf) alignment = leaf->align;
  out->storage_size = storage_size;
  out->scalar_element_size = scalar_size;
  out->alignment = alignment;
  return 1;
}

static int type_chain_contains_vla(const psx_type_t *type) {
  for (const psx_type_t *cursor = type; cursor; cursor = cursor->base) {
    if (cursor->is_vla) return 1;
  }
  return 0;
}

int psx_plan_complete_object_storage(
    const psx_type_t *type, psx_complete_object_storage_plan_t *out) {
  if (!type || !out || type->kind == PSX_TYPE_ARRAY ||
      type->kind == PSX_TYPE_FUNCTION || type->kind == PSX_TYPE_VOID ||
      type_chain_contains_vla(type)) return 0;
  int storage_size = ps_type_sizeof(type);
  if (storage_size <= 0) return 0;

  int element_size = storage_size;
  if (type->kind == PSX_TYPE_POINTER) {
    element_size = ps_type_deref_size(type);
    if (element_size <= 0 && type->base)
      element_size = ps_type_sizeof(type->base);
    if (element_size <= 0) element_size = storage_size;
  }
  int alignment = type->align;
  if (alignment <= 0)
    alignment = storage_size >= 8 ? 8
                                  : (storage_size >= 4 ? 4
                                                       : (storage_size >= 2 ? 2 : 1));
  out->storage_size = storage_size;
  out->element_size = element_size;
  out->alignment = alignment;
  return 1;
}
