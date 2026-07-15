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

/* 集合体メンバ情報は semantic_ctx 側の統合 API (tag_member_info_t) を
 * そのまま再利用する (Phase A1 リファクタリング)。 */

node_t *ps_decl_bind_initializer_for_var_in(
    arena_context_t *arena_context,
    lvar_t *var, node_t *initializer,
    psx_decl_init_kind_t initializer_kind, token_t *init_tok) {
  node_t *target =
      ps_lvar_is_array(var) || ps_lvar_is_tag_aggregate(var)
          ? psx_node_new_lvar_object_ref_for_in(arena_context, var)
          : ps_node_new_lvar_expr_ref_for_in(arena_context, var);
  return psx_node_new_raw_decl_initializer_in(
      arena_context, target, initializer, initializer_kind, init_tok);
}

node_t *psx_decl_parse_initializer_for_var_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations,
    lvar_t *var) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  tokenizer_context_t *tokenizer_context =
      ps_parser_runtime_tokenizer(runtime_context);
  if (!tokenizer_context) return NULL;
  token_t *current_token =
      tk_get_current_token_ctx(tokenizer_context);
  if (current_token && current_token->kind == TK_LBRACE) {
    token_t *init_tok = current_token;
    node_t *syntax = psx_parse_initializer_syntax_list_in_contexts(
        semantic_context, global_registry, local_registry,
        runtime_context,
        local_declarations);
    return ps_decl_bind_initializer_for_var_in(
        ps_parser_runtime_arena(runtime_context),
        var, syntax, PSX_DECL_INIT_LIST, init_tok);
  }
  token_t *init_tok = current_token;
  return ps_decl_bind_initializer_for_var_in(
      ps_parser_runtime_arena(runtime_context), var,
      psx_expr_assign_in_contexts(
          semantic_context, global_registry, local_registry,
          runtime_context,
          local_declarations),
      PSX_DECL_INIT_EXPR, init_tok);
}
