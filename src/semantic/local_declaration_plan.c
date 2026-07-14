#include "local_declaration_plan.h"

int psx_plan_local_storage(
    const psx_type_t *type, psx_local_storage_plan_t *out) {
  if (!type || !out || type->kind == PSX_TYPE_FUNCTION ||
      type->kind == PSX_TYPE_VOID ||
      ps_type_contains_vla_array(type)) return 0;
  const psx_type_t *leaf = type;
  while (leaf && leaf->kind == PSX_TYPE_ARRAY) {
    if (leaf->array_len <= 0 || ps_type_sizeof(leaf) <= 0) return 0;
    leaf = leaf->base;
  }
  int storage_size = ps_type_sizeof(type);
  if (storage_size <= 0) return 0;

  int alignment = type->align;
  if (alignment <= 0 && leaf) alignment = leaf->align;
  if (alignment <= 0) {
    alignment = storage_size >= 8 ? 8
                                  : (storage_size >= 4 ? 4
                                                       : (storage_size >= 2 ? 2 : 1));
  }
  out->storage_size = storage_size;
  out->alignment = alignment;
  return 1;
}
