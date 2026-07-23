#include "hir_local_resolution.h"

#include "../parser/arena.h"
#include "../parser/lvar_public.h"
#include "../parser/semantic_ctx.h"
#include "../parser/vla_runtime.h"

static int apply_vla_hir_node_spec(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *local, int include_indirect_parameter_metadata,
    psx_hir_node_spec_t *spec) {
  if (!semantic_context || !local || !spec) return 0;
  if (!ps_lvar_is_vla(local)) return 1;
  if (!include_indirect_parameter_metadata &&
      ps_lvar_vla_pointer_indirections(local) > 0)
    return 1;

  spec->vla_stride_frame_offset =
      ps_lvar_vla_row_stride_frame_off(local);
  spec->vla_stride_source_offset =
      ps_lvar_vla_row_stride_src_offset(local);
  spec->vla_stride_element_size =
      ps_lvar_vla_row_stride_elem_size(local);
  spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
  int count = ps_lvar_vla_param_inner_dim_count(local);
  if (count <= 0) return 1;

  arena_context_t *arena_context = ps_ctx_arena(semantic_context);
  int *constants = arena_alloc_in(
      arena_context, (size_t)count * sizeof(*constants));
  int *source_offsets = arena_alloc_in(
      arena_context, (size_t)count * sizeof(*source_offsets));
  if (!constants || !source_offsets) return 0;
  for (int i = 0; i < count; i++) {
    constants[i] = ps_lvar_vla_param_inner_dim_const(local, i);
    source_offsets[i] =
        ps_lvar_vla_param_inner_dim_src_offset(local, i);
  }
  spec->vla_dimension_constants = constants;
  spec->vla_dimension_source_offsets = source_offsets;
  spec->vla_dimension_count = (size_t)count;
  return 1;
}

int psx_apply_local_vla_hir_node_spec_in(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *local, psx_hir_node_spec_t *spec) {
  return apply_vla_hir_node_spec(
      semantic_context, local, 0, spec);
}

static int resolve_local_hir_node_spec(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *local, int storage_offset,
    int include_indirect_parameter_metadata,
    psx_hir_node_spec_t *spec) {
  if (!semantic_context || !local || !spec) return 0;
  spec->kind = PSX_HIR_LOCAL;
  spec->attached_qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  spec->storage_offset = storage_offset;
  spec->object_offset = ps_lvar_offset(local);
  spec->object_size = ps_lvar_frame_storage_size(local);
  spec->object_align = ps_lvar_align_bytes(local);
  return apply_vla_hir_node_spec(
      semantic_context, local,
      include_indirect_parameter_metadata, spec);
}

int psx_resolve_local_hir_node_spec_in(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *local, int storage_offset,
    psx_hir_node_spec_t *spec) {
  return resolve_local_hir_node_spec(
      semantic_context, local, storage_offset, 0, spec);
}

int psx_resolve_parameter_hir_node_spec_in(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *parameter, int storage_offset,
    psx_hir_node_spec_t *spec) {
  if (!ps_lvar_is_param(parameter)) return 0;
  return resolve_local_hir_node_spec(
      semantic_context, parameter, storage_offset, 1, spec);
}
