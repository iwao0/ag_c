#include "local_declaration_plan.h"
#include "../type_layout.h"

int psx_plan_local_storage_for_type_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id,
    const ag_target_info_t *target,
    psx_local_storage_plan_t *out) {
  const psx_type_t *type = psx_semantic_type_table_lookup(types, type_id);
  if (!type || !out || type->kind == PSX_TYPE_FUNCTION ||
      type->kind == PSX_TYPE_VOID ||
      ps_type_contains_vla_array(type)) return 0;
  int storage_size = ps_type_sizeof_id_with_records(
      types, record_layouts, type_id, target);
  if (storage_size <= 0) return 0;

  int alignment = ps_type_alignof_id_with_records(
      types, record_layouts, type_id, target);
  if (alignment <= 0) return 0;
  out->storage_size = storage_size;
  out->alignment = alignment;
  return 1;
}
