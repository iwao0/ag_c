#include "hir_symbol_resolution.h"

#include "../parser/gvar_public.h"
#include "../parser/semantic_ctx.h"
#include "../type_layout.h"

int psx_resolve_global_hir_symbol_spec_in(
    const psx_semantic_context_t *semantic_context,
    const global_var_t *global,
    psx_hir_symbol_spec_t *symbol) {
  if (!semantic_context || !global || !symbol) return 0;
  psx_qual_type_t qual_type = ps_gvar_decl_qual_type(global);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !ps_ctx_type_by_id_in(semantic_context, qual_type.type_id))
    return 0;

  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(semantic_context);
  const ag_target_info_t *target =
      ps_ctx_target_info(semantic_context);
  int byte_size = ps_type_sizeof_id(
      semantic_types, record_layouts, qual_type.type_id, target);
  int alignment = ps_type_alignof_id(
      semantic_types, record_layouts, qual_type.type_id, target);
  if ((byte_size <= 0 || alignment <= 0) &&
      ps_gvar_is_extern_decl(global)) {
    psx_qual_type_t base = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    if (base.type_id != PSX_TYPE_ID_INVALID) {
      if (byte_size <= 0)
        byte_size = ps_type_sizeof_id(
            semantic_types, record_layouts, base.type_id, target);
      if (alignment <= 0)
        alignment = ps_type_alignof_id(
            semantic_types, record_layouts, base.type_id, target);
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
