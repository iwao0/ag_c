#include "local_declaration_plan.h"
#include "../type_layout.h"

int psx_plan_local_storage(
    const psx_type_t *type, psx_local_storage_plan_t *out) {
  ag_target_info_t target = ag_target_info_host();
  return psx_plan_local_storage_for_target(type, &target, out);
}

int psx_plan_local_storage_for_target(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_local_storage_plan_t *out) {
  if (!type || !out || type->kind == PSX_TYPE_FUNCTION ||
      type->kind == PSX_TYPE_VOID ||
      ps_type_contains_vla_array(type)) return 0;
  const psx_type_t *leaf = type;
  while (leaf && leaf->kind == PSX_TYPE_ARRAY) {
    if (leaf->array_len <= 0 ||
        ps_type_sizeof_for_target(leaf, target) <= 0) return 0;
    leaf = leaf->base;
  }
  int storage_size = ps_type_sizeof_for_target(type, target);
  if (storage_size <= 0) return 0;

  int alignment = ps_type_alignof_for_target(type, target);
  if (alignment <= 0 && leaf)
    alignment = ps_type_alignof_for_target(leaf, target);
  if (alignment <= 0) {
    alignment = storage_size >= 8 ? 8
                                  : (storage_size >= 4 ? 4
                                                       : (storage_size >= 2 ? 2 : 1));
  }
  out->storage_size = storage_size;
  out->alignment = alignment;
  return 1;
}
