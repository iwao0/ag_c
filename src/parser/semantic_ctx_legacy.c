#include "semantic_ctx_legacy.h"

#include "type_builder.h"
#include "type_owned_internal.h"
#include "../semantic/type_compatibility_view.h"

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                           PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t ps_ctx_intern_qual_type_in(
    psx_semantic_context_t *context, const psx_type_t *type) {
  psx_semantic_type_table_t *types =
      (psx_semantic_type_table_t *)
          ps_ctx_semantic_type_table_in(context);
  return types && type
             ? psx_semantic_type_table_intern(types, type)
             : invalid_qual_type();
}

psx_qual_type_t ps_ctx_find_interned_qual_type_in(
    const psx_semantic_context_t *context, const psx_type_t *type) {
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(context);
  return types && type
             ? psx_semantic_type_table_find(types, type)
             : invalid_qual_type();
}

const psx_type_t *ps_ctx_type_by_id_in(
    const psx_semantic_context_t *context, psx_type_id_t type_id) {
  return psx_type_compatibility_canonical_view_for(
      ps_ctx_semantic_type_table_in(context), type_id);
}

psx_type_t *ps_ctx_clone_tag_type_at_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    psx_scope_lookup_point_t point) {
  psx_qual_type_t type = ps_ctx_tag_qual_type_at_in(
      context, kind, name, len, point);
  const psx_type_t *view = psx_type_compatibility_view_for(
      ps_ctx_semantic_type_table_in(context), type);
  return view ? ps_type_clone_in(ps_ctx_arena(context), view) : NULL;
}

/* Compatibility-only import of parser type trees for parser tests. */
void ps_ctx_bind_record_ids_in(
    psx_semantic_context_t *context, psx_type_t *type) {
  if (!context || !type) return;
  if (ps_type_is_tag_aggregate(type)) {
    psx_record_id_t record_id = type->record_id;
    if (record_id == PSX_RECORD_ID_INVALID && type->tag_name &&
        type->tag_len > 0) {
      record_id = ps_ctx_resolve_tag_record_id_in(
          context, ps_type_tag_token_kind(type),
          type->tag_name, type->tag_len);
    }
    type->record_id = record_id;
  }
  ps_ctx_bind_record_ids_in(context, psx_type_owned_base_mut(type));
  for (int i = 0; i < type->param_count; i++)
    ps_ctx_bind_record_ids_in(
        context, psx_type_owned_param_mut(type, i));
}

psx_qual_type_t ps_ctx_intern_declaration_qual_type_in(
    psx_semantic_context_t *context, const psx_type_t *type) {
  if (!context || !type) return invalid_qual_type();
  psx_type_t *resolved_type = ps_type_clone_in(ps_ctx_arena(context), type);
  if (!resolved_type) return invalid_qual_type();
  ps_ctx_bind_record_ids_in(context, resolved_type);
  return ps_ctx_intern_qual_type_in(context, resolved_type);
}
