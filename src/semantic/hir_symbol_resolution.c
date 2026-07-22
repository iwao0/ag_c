#include "hir_symbol_resolution.h"

#include "../parser/gvar_public.h"
#include "../parser/semantic_ctx.h"
#include "../type_layout.h"
#include "type_identity.h"

int psx_resolve_global_hir_symbol_spec_in(
    const psx_semantic_context_t *semantic_context,
    const global_var_t *global,
    psx_hir_symbol_spec_t *symbol) {
  if (!semantic_context || !global || !symbol) return 0;
  psx_qual_type_t qual_type = ps_gvar_decl_qual_type(global);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (!psx_semantic_type_table_qual_type_is_valid(
          semantic_types, qual_type))
    return 0;
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(semantic_context);
  const ag_data_layout_t *data_layout = ps_ctx_data_layout(semantic_context);
  int byte_size = psx_qual_type_layout_sizeof(
      semantic_types, record_layouts, qual_type, data_layout);
  int alignment = psx_qual_type_layout_alignof(
      semantic_types, record_layouts, qual_type, data_layout);
  int requested_alignment = ps_gvar_requested_alignment(global);
  if (requested_alignment > alignment)
    alignment = requested_alignment;
  psx_type_shape_t shape = {0};
  int has_shape = psx_semantic_type_table_describe(
      semantic_types, qual_type.type_id, &shape);
  int is_incomplete_array = has_shape && shape.kind == PSX_TYPE_ARRAY &&
                            shape.array_len <= 0 && !shape.is_vla;
  int is_incomplete_record =
      has_shape && psx_type_kind_is_aggregate(shape.kind) && byte_size <= 0;
  /* Function bodies may refer to the array before a later declaration
   * completes its bound. The element size is sufficient for that access. */
  if ((byte_size <= 0 || alignment <= 0) && is_incomplete_array) {
    psx_qual_type_t base = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    if (base.type_id != PSX_TYPE_ID_INVALID) {
      if (byte_size <= 0)
        byte_size = psx_qual_type_layout_sizeof(
            semantic_types, record_layouts, base, data_layout);
      if (alignment <= 0)
        alignment = psx_qual_type_layout_alignof(
            semantic_types, record_layouts, base, data_layout);
    }
  }
  /* An incomplete record object may likewise be named only for operations
   * that do not require its layout (notably address-of). HIR symbols retain
   * a nonzero opaque storage marker until the shared RecordId is completed. */
  if ((byte_size <= 0 || alignment <= 0) && is_incomplete_record) {
    byte_size = 1;
    alignment = 1;
  }
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_lookup_declaration_in_scope(
          ps_ctx_scope_graph(semantic_context),
          PSX_SCOPE_ID_TRANSLATION_UNIT, PSX_NAMESPACE_ORDINARY,
          ps_gvar_name(global), ps_gvar_name_len(global));
  if (!declaration || declaration->payload != global) return 0;
  *symbol = (psx_hir_symbol_spec_t){
      .name = ps_gvar_name(global),
      .name_length = ps_gvar_name_len(global) > 0
                         ? (size_t)ps_gvar_name_len(global) : 0,
      .declaration_id = declaration->id,
      .qual_type = qual_type,
      .byte_size = byte_size,
      .alignment = alignment,
      .is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0,
      .is_static = ps_gvar_is_static_storage(global) ? 1 : 0,
      .is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0,
  };
  return 1;
}
