#include "compound_literal_semantics.h"

#include <string.h>

#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "../type_layout.h"

psx_compound_literal_storage_duration_t
psx_compound_literal_storage_duration_in_scope_graph(
    const psx_scope_graph_t *scope_graph,
    psx_scope_id_t lexical_scope,
    int inside_function_body) {
  psx_scope_id_t function_scope =
      psx_scope_graph_nearest_scope_of_kind(
          scope_graph, lexical_scope, PSX_SCOPE_FUNCTION);
  return inside_function_body ||
                 function_scope != PSX_SCOPE_ID_INVALID
             ? PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC
             : PSX_COMPOUND_LITERAL_STORAGE_STATIC;
}

int psx_resolve_compound_literal_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t object_qual_type,
    psx_scope_id_t lexical_scope,
    int inside_function_body,
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
      ps_type_sizeof_id(
          ps_ctx_semantic_type_table_in(semantic_context),
          ps_ctx_record_layout_table_in(semantic_context),
          object_qual_type.type_id,
          ps_ctx_target_info(semantic_context)) <= 0)
    return 0;

  *plan = (psx_compound_literal_plan_t){
      .storage_duration =
          psx_compound_literal_storage_duration_in_scope_graph(
              ps_ctx_scope_graph(semantic_context), lexical_scope,
              inside_function_body),
      .object_qual_type = object_qual_type,
  };
  return 1;
}
