#include "decl.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "expr.h"
#include "initializer_syntax.h"
#include "lvar_internal.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "config_runtime.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *current_funcname;
static int current_funcname_len;
static inline token_t *curtok(void) { return tk_get_current_token(); }

void ps_decl_set_current_funcname(char *name, int len) {
  current_funcname = name;
  current_funcname_len = len;
}

void ps_decl_get_current_funcname(char **out_name, int *out_len) {
  if (out_name) *out_name = current_funcname;
  if (out_len) *out_len = current_funcname_len;
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

int ps_lvar_storage_size(const lvar_t *var, int fallback_size) {
  int decl_size = ps_lvar_decl_sizeof(var, 0);
  int storage_size = (var && var->size > 0) ? var->size : 0;
  if (storage_size > decl_size) return storage_size;
  if (decl_size > 0) return decl_size;
  return storage_size > 0 ? storage_size : fallback_size;
}

int ps_lvar_decl_sizeof(const lvar_t *var, int fallback_size) {
  const psx_type_t *type = lvar_public_decl_type(var);
  int decl_size = ps_type_sizeof(type);
  return decl_size > 0 ? decl_size : fallback_size;
}

int ps_lvar_elem_size(const lvar_t *var, int fallback_size) {
  const psx_type_t *type = lvar_public_decl_type(var);
  int size = ps_type_deref_size(type);
  return size > 0 ? size : fallback_size;
}

int ps_lvar_array_flat_element_count(const lvar_t *var) {
  return var ? ps_type_array_flat_element_count(lvar_public_decl_type(var)) : 0;
}

int ps_lvar_array_scalar_element_size(const lvar_t *var) {
  if (!ps_lvar_is_array(var)) return ps_lvar_elem_size(var, 0);
  int elem = ps_type_array_scalar_element_size(lvar_public_decl_type(var));
  if (elem > 0) return elem;
  return ps_lvar_elem_size(var, 0);
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
    return leaf->fp_kind;
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

void ps_decl_reset_translation_unit_state(void) {
  ps_decl_reset_locals();
  current_funcname = NULL;
  current_funcname_len = 0;
  psx_declaration_pipeline_reset_translation_unit_state();
}

/* 集合体メンバ情報は semantic_ctx 側の統合 API (tag_member_info_t) を
 * そのまま再利用する (Phase A1 リファクタリング)。 */

node_t *ps_decl_bind_initializer_for_var(
    lvar_t *var, node_t *initializer,
    psx_decl_init_kind_t initializer_kind, token_t *init_tok) {
  node_t *target =
      ps_lvar_is_array(var) || ps_lvar_is_tag_aggregate(var)
          ? psx_node_new_lvar_object_ref_for(var)
          : ps_node_new_lvar_expr_ref_for(var);
  return psx_node_new_raw_decl_initializer(
      target, initializer, initializer_kind, init_tok);
}

node_t *psx_decl_parse_initializer_for_var(lvar_t *var) {
  if (curtok() && curtok()->kind == TK_LBRACE) {
    token_t *init_tok = curtok();
    node_t *syntax = psx_parse_initializer_syntax_list();
    return ps_decl_bind_initializer_for_var(
        var, syntax, PSX_DECL_INIT_LIST, init_tok);
  }
  token_t *init_tok = curtok();
  return ps_decl_bind_initializer_for_var(
      var, psx_expr_assign(), PSX_DECL_INIT_EXPR, init_tok);
}
