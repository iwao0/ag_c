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

static const psx_type_t *lvar_public_decl_type(const lvar_t *var) {
  return var ? ps_lvar_get_decl_type(var) : NULL;
}

static const psx_type_t *lvar_public_pointee_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER ? type->base : NULL;
}

static token_kind_t lvar_public_tag_kind_from_type(const psx_type_t *type) {
  if (!type) return TK_EOF;
  type = ps_type_array_leaf_type(type);
  if (type && type->kind == PSX_TYPE_POINTER) type = type->base;
  type = ps_type_array_leaf_type(type);
  if (!type) return TK_EOF;
  if (type->kind == PSX_TYPE_STRUCT) return TK_STRUCT;
  if (type->kind == PSX_TYPE_UNION) return TK_UNION;
  return TK_EOF;
}

int ps_lvar_frame_storage_size(const lvar_t *var) {
  return var && var->size > 0 ? var->size : 0;
}

int ps_lvar_array_flat_element_count(const lvar_t *var) {
  return var ? ps_type_array_flat_element_count(lvar_public_decl_type(var)) : 0;
}

int ps_lvar_array_designator_stride_elements(const lvar_t *var, int depth) {
  if (depth < 0) return 1;
  int stride = var ? ps_type_array_subscript_stride_elements(
                         lvar_public_decl_type(var), depth)
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
  const psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_contains_vla_array(type) ||
         (var && var->vla_runtime.view.row_stride_frame_off != 0);
}

int ps_lvar_is_array(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return type && type->kind == PSX_TYPE_ARRAY ? 1 : 0;
}

int ps_lvar_is_complex(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *leaf = ps_type_array_leaf_type(type);
  return leaf && leaf->kind == PSX_TYPE_COMPLEX ? 1 : 0;
}

int ps_lvar_is_tag_pointer(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *base = lvar_public_pointee_type(type);
  return base ? ps_type_is_tag_aggregate(ps_type_array_leaf_type(base)) : 0;
}

token_kind_t ps_lvar_tag_kind(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return lvar_public_tag_kind_from_type(type);
}

tk_float_kind_t ps_lvar_fp_kind(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *leaf = ps_type_array_leaf_type(type);
  if (leaf && (leaf->kind == PSX_TYPE_FLOAT || leaf->kind == PSX_TYPE_COMPLEX))
    return ps_type_floating_token_kind(leaf);
  return TK_FLOAT_KIND_NONE;
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

void ps_decl_reset_translation_unit_state_in(
    psx_local_registry_t *registry) {
  if (!registry) return;
  ps_decl_reset_locals_in(registry);
  ps_decl_set_current_funcname_in(registry, NULL, 0);
}
