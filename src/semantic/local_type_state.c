#include "local_type_state.h"

#include "../parser/decl.h"
#include "../parser/node_utils.h"
#include "../parser/type.h"
#include <stdlib.h>

static void clear_array_stride_cache(lvar_t *var) {
  if (!var) return;
  var->outer_stride = 0;
  var->mid_stride = 0;
  var->extra_strides_count = 0;
  if (var->extra_strides) {
    for (int i = 0; i < 5; i++) var->extra_strides[i] = 0;
  }
}

static void sync_array_stride_cache(lvar_t *var) {
  if (!var) return;
  int outer = 0;
  int mid = 0;
  int extras[5] = {0};
  int extra_count = 0;
  clear_array_stride_cache(var);
  if (!ps_type_decl_array_stride_metadata(
          var->decl_type, &outer, &mid, extras, &extra_count)) {
    return;
  }
  var->outer_stride = outer;
  var->mid_stride = mid;
  if (extra_count > 0 && !var->extra_strides)
    var->extra_strides = calloc(5, sizeof(int));
  var->extra_strides_count = (unsigned char)extra_count;
  for (int i = 0; i < extra_count; i++)
    var->extra_strides[i] = extras[i];
}

static void sync_type_cache(lvar_t *var) {
  if (!var || !var->decl_type) return;
  const psx_type_t *type = var->decl_type;
  var->is_array = type->kind == PSX_TYPE_ARRAY;
  var->is_vla = type->is_vla ? 1 : 0;
  sync_array_stride_cache(var);
}

void psx_decl_set_lvar_decl_type(
    lvar_t *var, const psx_type_t *decl_type) {
  if (!var || !decl_type) return;
  var->decl_type = ps_type_clone_persistent(decl_type);
  sync_type_cache(var);
}

void psx_decl_set_lvar_vla_descriptor(
    lvar_t *var, int outer_stride, int row_stride_frame_off,
    int strides_remaining, int row_stride_src_offset,
    int row_stride_elem_size) {
  if (!var) return;
  psx_type_t *type = ps_lvar_get_decl_type(var);
  var->is_vla = 1;
  var->outer_stride = outer_stride;
  var->vla_row_stride_frame_off = row_stride_frame_off;
  var->vla_strides_remaining = strides_remaining;
  var->vla_row_stride_src_offset = row_stride_src_offset;
  var->vla_row_stride_elem_size = (short)row_stride_elem_size;
  if (!type) return;
  if (var->is_array && type->kind != PSX_TYPE_POINTER) {
    psx_type_t *vla = ps_type_new_vla_object_view(
        type, outer_stride, row_stride_frame_off, strides_remaining);
    if (vla) {
      var->decl_type = vla;
      type = vla;
    }
  } else if (!var->is_array && type->kind == PSX_TYPE_POINTER &&
             row_stride_frame_off != 0 && type->base &&
             type->base->kind != PSX_TYPE_ARRAY) {
    int elem_size = row_stride_elem_size > 0
                        ? row_stride_elem_size
                        : ps_type_sizeof(type->base);
    if (elem_size > 0) {
      psx_type_t *row = ps_type_new_array(
          type->base, 0, 0, elem_size, 1);
      row->base_deref_size = elem_size;
      row->outer_stride = elem_size;
      type->base = row;
      type->deref_size = 0;
      type->base_deref_size = elem_size;
    }
  }
  type->outer_stride = outer_stride;
  ps_type_set_vla_runtime_descriptor(
      type, row_stride_frame_off, strides_remaining,
      row_stride_src_offset, row_stride_elem_size);
}

void psx_decl_set_lvar_vla_param_inner_dims(
    lvar_t *var, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count) {
  if (!var) return;
  if (inner_dim_count < 0) inner_dim_count = 0;
  if (inner_dim_count > 7) inner_dim_count = 7;
  var->vla_param_inner_dim_count = (unsigned char)inner_dim_count;
  for (int i = 0; i < 7; i++) {
    var->vla_param_inner_dim_consts[i] =
        (i < inner_dim_count && inner_dim_consts)
            ? (short)inner_dim_consts[i]
            : 0;
    var->vla_param_inner_dim_src_offsets[i] =
        (i < inner_dim_count && inner_dim_src_offsets)
            ? inner_dim_src_offsets[i]
            : 0;
  }
  ps_type_set_vla_param_inner_dims(
      ps_lvar_get_decl_type(var), inner_dim_consts,
      inner_dim_src_offsets, inner_dim_count);
}
