#include "decl.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "expr.h"
#include "initializer_syntax.h"
#include "lvar_internal.h"
#include "local_registry.h"
#include "node_utils.h"
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../semantic/resolved_object_ref.h"
#include "../semantic/type_compatibility_view.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ps_decl_reset_locals_in(psx_local_registry_t *registry) {
  if (!registry) return;
  ps_local_registry_reset_in(registry);
}

void ps_decl_set_current_funcname_in(
    psx_local_registry_t *registry, char *name, int len) {
  ps_local_registry_set_current_function_in(registry, name, len);
}

void ps_decl_get_current_funcname_in(
    const psx_local_registry_t *registry,
    char **out_name, int *out_len) {
  ps_local_registry_get_current_function_in(
      registry, out_name, out_len);
}

static int lvar_decl_shape(const lvar_t *var, psx_type_shape_t *shape) {
  return var && shape && psx_semantic_type_table_describe(
      var->decl_type_table, var->decl_qual_type.type_id, shape);
}

static int lvar_array_leaf_shape(const lvar_t *var,
                                 psx_type_shape_t *shape) {
  if (!var || !shape) return 0;
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      var->decl_type_table, var->decl_qual_type.type_id);
  return psx_semantic_type_table_describe(
      var->decl_type_table, leaf.type_id, shape);
}

static token_kind_t semantic_record_kind(psx_type_kind_t kind) {
  if (kind == PSX_TYPE_STRUCT) return TK_STRUCT;
  if (kind == PSX_TYPE_UNION) return TK_UNION;
  return TK_EOF;
}

static tk_float_kind_t semantic_floating_kind(
    const psx_type_shape_t *shape) {
  if (!shape || (shape->kind != PSX_TYPE_FLOAT &&
                 shape->kind != PSX_TYPE_COMPLEX))
    return TK_FLOAT_KIND_NONE;
  if (shape->floating_kind == PSX_FLOATING_KIND_FLOAT)
    return TK_FLOAT_KIND_FLOAT;
  if (shape->floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE)
    return TK_FLOAT_KIND_LONG_DOUBLE;
  return shape->floating_kind == PSX_FLOATING_KIND_DOUBLE
             ? TK_FLOAT_KIND_DOUBLE
             : TK_FLOAT_KIND_NONE;
}

static const psx_type_t *lvar_decl_type_consistent(const lvar_t *var) {
  return var ? psx_type_compatibility_view_for(
                   var->decl_type_table, var->decl_qual_type)
             : NULL;
}

const psx_type_t *ps_lvar_get_decl_type(const lvar_t *var) {
  return lvar_decl_type_consistent(var);
}

psx_qual_type_t ps_lvar_decl_qual_type(const lvar_t *var) {
  return var ? var->decl_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

psx_type_id_t ps_lvar_decl_type_id(const lvar_t *var) {
  return ps_lvar_decl_qual_type(var).type_id;
}

int ps_lvar_frame_storage_size(const lvar_t *var) {
  return var && var->size > 0 ? var->size : 0;
}

int ps_lvar_array_flat_element_count(const lvar_t *var) {
  return var ? psx_semantic_type_table_array_flat_element_count(
                   var->decl_type_table, var->decl_qual_type.type_id)
             : 0;
}

int ps_lvar_array_designator_stride_elements(const lvar_t *var, int depth) {
  if (depth < 0) return 1;
  int stride = var ? psx_semantic_type_table_array_subscript_stride_elements(
                         var->decl_type_table, var->decl_qual_type.type_id,
                         depth)
                   : 0;
  return stride > 0 ? stride : 1;
}

int ps_lvar_align_bytes(const lvar_t *var) {
  return var ? var->align_bytes : 0;
}

int ps_lvar_is_param(const lvar_t *var) {
  return (var && var->is_param) ? 1 : 0;
}

int ps_lvar_is_static_local(const lvar_t *var) {
  return (var && var->is_static_local) ? 1 : 0;
}

global_var_t *ps_lvar_static_storage_global(const lvar_t *var) {
  return var && var->is_static_local ? var->static_global : NULL;
}

int ps_lvar_is_vla(const lvar_t *var) {
  return (var && psx_semantic_type_table_contains_vla_array(
                     var->decl_type_table, var->decl_qual_type.type_id)) ||
         (var && var->vla_runtime.view.row_stride_frame_off != 0);
}

int ps_lvar_is_array(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  return lvar_decl_shape(var, &shape) && shape.kind == PSX_TYPE_ARRAY;
}

int ps_lvar_is_complex(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  return lvar_array_leaf_shape(var, &shape) &&
         shape.kind == PSX_TYPE_COMPLEX;
}

int ps_lvar_is_tag_pointer(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  if (!lvar_decl_shape(var, &shape) || shape.kind != PSX_TYPE_POINTER)
    return 0;
  psx_qual_type_t base = psx_semantic_type_table_base(
      var->decl_type_table, var->decl_qual_type.type_id);
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      var->decl_type_table, base.type_id);
  return psx_semantic_type_table_describe(
             var->decl_type_table, leaf.type_id, &shape) &&
         psx_type_kind_is_aggregate(shape.kind);
}

token_kind_t ps_lvar_tag_kind(const lvar_t *var) {
  if (!var) return TK_EOF;
  psx_qual_type_t type = psx_semantic_type_table_array_leaf(
      var->decl_type_table, var->decl_qual_type.type_id);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          var->decl_type_table, type.type_id, &shape))
    return TK_EOF;
  if (shape.kind == PSX_TYPE_POINTER) {
    type = psx_semantic_type_table_base(
        var->decl_type_table, type.type_id);
    type = psx_semantic_type_table_array_leaf(
        var->decl_type_table, type.type_id);
    if (!psx_semantic_type_table_describe(
            var->decl_type_table, type.type_id, &shape))
      return TK_EOF;
  }
  return semantic_record_kind(shape.kind);
}

tk_float_kind_t ps_lvar_fp_kind(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  return lvar_array_leaf_shape(var, &shape)
             ? semantic_floating_kind(&shape)
             : TK_FLOAT_KIND_NONE;
}

int ps_lvar_value_is_pointer_like(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  return lvar_decl_shape(var, &shape) &&
         (shape.kind == PSX_TYPE_POINTER || shape.kind == PSX_TYPE_ARRAY);
}

int ps_lvar_is_struct_aggregate(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  return lvar_array_leaf_shape(var, &shape) &&
         shape.kind == PSX_TYPE_STRUCT;
}

int ps_lvar_is_union_aggregate(const lvar_t *var) {
  psx_type_shape_t shape = {0};
  return lvar_array_leaf_shape(var, &shape) &&
         shape.kind == PSX_TYPE_UNION;
}

int ps_lvar_is_tag_aggregate(const lvar_t *var) {
  return ps_lvar_is_struct_aggregate(var) || ps_lvar_is_union_aggregate(var);
}

int ps_lvar_vla_row_stride_frame_off(const lvar_t *var) {
  return var ? var->vla_runtime.view.row_stride_frame_off : 0;
}

int ps_lvar_vla_strides_remaining(const lvar_t *var) {
  return var && var->vla_runtime.view.strides_remaining > 0
             ? var->vla_runtime.view.strides_remaining
             : 0;
}

int ps_lvar_vla_row_stride_elem_size(const lvar_t *var) {
  return var && var->vla_runtime.row_stride_elem_size > 0
             ? var->vla_runtime.row_stride_elem_size
             : 0;
}

int ps_lvar_vla_row_stride_src_offset(const lvar_t *var) {
  return var ? var->vla_runtime.row_stride_src_offset : 0;
}

int ps_lvar_vla_param_inner_dim_count(const lvar_t *var) {
  return var ? var->vla_runtime.param_inner_dim_count : 0;
}

int ps_lvar_vla_param_inner_dim_const(const lvar_t *var, int idx) {
  if (!var || idx < 0 || idx >= var->vla_runtime.param_inner_dim_count ||
      !var->vla_runtime.param_inner_dim_consts)
    return 0;
  return var->vla_runtime.param_inner_dim_consts[idx];
}

int ps_lvar_vla_param_inner_dim_src_offset(const lvar_t *var, int idx) {
  if (!var || idx < 0 || idx >= var->vla_runtime.param_inner_dim_count ||
      !var->vla_runtime.param_inner_dim_src_offsets)
    return 0;
  return var->vla_runtime.param_inner_dim_src_offsets[idx];
}
