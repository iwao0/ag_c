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
  int byte_size = psx_type_layout_sizeof(semantic_types, record_layouts,
                                         qual_type.type_id, data_layout);
  int alignment = psx_type_layout_alignof(semantic_types, record_layouts,
                                          qual_type.type_id, data_layout);
  psx_type_shape_t shape = {0};
  int is_incomplete_array = psx_semantic_type_table_describe(
      semantic_types, qual_type.type_id, &shape) &&
      shape.kind == PSX_TYPE_ARRAY && shape.array_len <= 0 && !shape.is_vla;
  /* Function bodies may refer to the array before a later declaration
   * completes its bound. The element size is sufficient for that access. */
  if ((byte_size <= 0 || alignment <= 0) && is_incomplete_array) {
    psx_qual_type_t base = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    if (base.type_id != PSX_TYPE_ID_INVALID) {
      if (byte_size <= 0)
        byte_size = psx_type_layout_sizeof(semantic_types, record_layouts,
                                           base.type_id, data_layout);
      if (alignment <= 0)
        alignment = psx_type_layout_alignof(semantic_types, record_layouts,
                                            base.type_id, data_layout);
    }
  }
  *symbol = (psx_hir_symbol_spec_t){
      .name = ps_gvar_name(global),
      .name_length = ps_gvar_name_len(global) > 0
                         ? (size_t)ps_gvar_name_len(global) : 0,
      .qual_type = qual_type,
      .byte_size = byte_size,
      .alignment = alignment,
      .is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0,
      .is_static = ps_gvar_is_static_storage(global) ? 1 : 0,
      .is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0,
  };
  return 1;
}
