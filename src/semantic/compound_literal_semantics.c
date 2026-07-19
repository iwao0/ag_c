#include "compound_literal_semantics.h"

#include <string.h>

#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "../type_layout.h"

int psx_resolve_compound_literal_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t object_qual_type,
    int has_file_scope_storage,
    int requires_address_result,
    psx_compound_literal_plan_t *plan) {
  if (plan) memset(plan, 0, sizeof(*plan));
  if (!semantic_context || !plan ||
      object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_type_t *object_type = ps_ctx_type_by_id_in(
      semantic_context, object_qual_type.type_id);
  if (!object_type || object_type->kind == PSX_TYPE_VOID ||
      object_type->kind == PSX_TYPE_FUNCTION ||
      ps_type_is_incomplete_array(object_type) ||
      ps_type_contains_vla_array(object_type) ||
      ps_type_sizeof_id_with_records(
          ps_ctx_semantic_type_table_in(semantic_context),
          ps_ctx_record_layout_table_in(semantic_context),
          object_qual_type.type_id,
          ps_ctx_target_info(semantic_context)) <= 0)
    return 0;

  psx_qual_type_t result_qual_type = object_qual_type;
  if (requires_address_result) {
    result_qual_type = ps_ctx_intern_pointer_to_qual_type_in(
        semantic_context, object_qual_type);
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  }
  *plan = (psx_compound_literal_plan_t){
      .storage_duration = has_file_scope_storage
                              ? PSX_COMPOUND_LITERAL_STORAGE_STATIC
                              : PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC,
      .object_qual_type = object_qual_type,
      .result_qual_type = result_qual_type,
      .yields_address = requires_address_result ? 1 : 0,
  };
  return 1;
}
