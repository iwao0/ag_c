#include "local_declaration_plan.h"
#include "../type_layout.h"

int psx_plan_local_storage_for_type_id(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target,
    psx_local_storage_plan_t *out) {
  const psx_type_t *type = psx_semantic_type_table_lookup(types, type_id);
  if (!type || !out || type->kind == PSX_TYPE_FUNCTION ||
      type->kind == PSX_TYPE_VOID ||
      ps_type_contains_vla_array(type)) return 0;
  const psx_type_t *leaf = type;
  while (leaf && leaf->kind == PSX_TYPE_ARRAY) {
    if (leaf->array_len <= 0) return 0;
    leaf = leaf->base;
  }
  int storage_size = ps_type_sizeof_id_for_target(types, type_id, target);
  if (storage_size <= 0) return 0;

  int alignment = ps_type_alignof_id_for_target(types, type_id, target);
  if (alignment <= 0 && leaf) {
    psx_qual_type_t leaf_identity =
        psx_semantic_type_table_find(types, leaf);
    alignment = ps_type_alignof_id_for_target(
        types, leaf_identity.type_id, target);
  }
  if (alignment <= 0) {
    alignment = storage_size >= 8 ? 8
                                  : (storage_size >= 4 ? 4
                                                       : (storage_size >= 2 ? 2 : 1));
  }
  out->storage_size = storage_size;
  out->alignment = alignment;
  return 1;
}
