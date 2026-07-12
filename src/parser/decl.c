#include "decl.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "expr.h"
#include "initializer_syntax.h"
#include "lvar_internal.h"
#include "node_utils.h"
#include "ret_pointee_array.h"
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

void psx_decl_get_current_funcname(char **out_name, int *out_len) {
  if (out_name) *out_name = current_funcname;
  if (out_len) *out_len = current_funcname_len;
}

static psx_type_t *lvar_public_decl_type(const lvar_t *var) {
  return var ? ps_lvar_get_decl_type((lvar_t *)var) : NULL;
}

static const psx_type_t *lvar_public_skip_arrays(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static const psx_type_t *lvar_public_pointee_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER ? type->base : NULL;
}

static token_kind_t lvar_public_tag_kind_from_type(const psx_type_t *type) {
  if (!type) return TK_EOF;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (type && type->kind == PSX_TYPE_POINTER) type = type->base;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
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
  psx_type_t *type = lvar_public_decl_type(var);
  int decl_size = ps_type_sizeof(type);
  return decl_size > 0 ? decl_size : fallback_size;
}

int ps_lvar_elem_size(const lvar_t *var, int fallback_size) {
  psx_type_t *type = lvar_public_decl_type(var);
  int size = ps_type_deref_size(type);
  return size > 0 ? size : fallback_size;
}

static int lvar_array_shape_from_decl_type(const psx_type_t *type,
                                           int *type_size,
                                           int *scalar_elem_size,
                                           int *stride_elems,
                                           int max_strides) {
  if (type_size) *type_size = 0;
  if (scalar_elem_size) *scalar_elem_size = 0;
  if (stride_elems) {
    for (int i = 0; i < max_strides; i++) stride_elems[i] = 0;
  }
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;

  int strides[10];
  int n = 0;
  const psx_type_t *cur = type;
  while (cur && cur->kind == PSX_TYPE_ARRAY && n < 10) {
    int stride = cur->base ? ps_type_sizeof(cur->base) : 0;
    if (stride <= 0) stride = ps_type_deref_size(cur);
    if (stride <= 0) break;
    strides[n++] = stride;
    cur = cur->base;
  }
  if (n <= 0) return 0;

  int elem = strides[n - 1];
  if (elem <= 0) return 0;
  if (type_size) *type_size = ps_type_sizeof(type);
  if (scalar_elem_size) *scalar_elem_size = elem;
  int out_count = n - 1;
  if (out_count > max_strides) out_count = max_strides;
  for (int i = 0; i < out_count; i++) {
    stride_elems[i] = strides[i] / elem;
    if (stride_elems[i] <= 0) stride_elems[i] = 1;
  }
  return n;
}

static int lvar_array_shape(const lvar_t *var, int *type_size,
                            int *scalar_elem_size, int *stride_elems,
                            int max_strides) {
  if (type_size) *type_size = 0;
  if (scalar_elem_size) *scalar_elem_size = 0;
  if (stride_elems) {
    for (int i = 0; i < max_strides; i++) stride_elems[i] = 0;
  }
  if (!var) return 0;
  psx_type_t *type = lvar_public_decl_type(var);
  int depth = lvar_array_shape_from_decl_type(type, type_size,
                                              scalar_elem_size,
                                              stride_elems,
                                              max_strides);
  return depth;
}

int ps_lvar_array_flat_element_count(const lvar_t *var) {
  if (!ps_lvar_is_array(var)) return 0;
  int type_size = 0;
  int elem = 0;
  (void)lvar_array_shape(var, &type_size, &elem, NULL, 0);
  if (type_size <= 0 || elem <= 0) return 0;
  return type_size / elem;
}

int ps_lvar_array_scalar_element_size(const lvar_t *var) {
  if (!ps_lvar_is_array(var)) return ps_lvar_elem_size(var, 0);
  int elem = 0;
  (void)lvar_array_shape(var, NULL, &elem, NULL, 0);
  if (elem > 0) return elem;
  return ps_lvar_elem_size(var, 0);
}

int ps_lvar_array_designator_stride_elements(const lvar_t *var, int depth) {
  if (depth < 0) return 1;
  int strides[8] = {0};
  int type_size = 0;
  int elem = 0;
  (void)lvar_array_shape(var, &type_size, &elem, strides, 8);
  (void)type_size;
  (void)elem;
  if (depth < 8 && strides[depth] > 0) return strides[depth];
  return 1;
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
  psx_type_t *type = lvar_public_decl_type(var);
  return type && type->is_vla ? 1 : 0;
}

int ps_lvar_is_array(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return type && type->kind == PSX_TYPE_ARRAY ? 1 : 0;
}

int ps_lvar_is_complex(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *leaf = lvar_public_skip_arrays(type);
  return leaf && leaf->kind == PSX_TYPE_COMPLEX ? 1 : 0;
}

int ps_lvar_is_tag_pointer(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *base = lvar_public_pointee_type(type);
  return base ? ps_type_is_tag_aggregate(lvar_public_skip_arrays(base)) : 0;
}

int ps_lvar_pointer_qual_levels(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_pointer_view_structural_qual_levels(type);
}

token_kind_t ps_lvar_tag_kind(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return lvar_public_tag_kind_from_type(type);
}

tk_float_kind_t ps_lvar_fp_kind(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *leaf = lvar_public_skip_arrays(type);
  if (leaf && (leaf->kind == PSX_TYPE_FLOAT || leaf->kind == PSX_TYPE_COMPLEX))
    return leaf->fp_kind;
  return TK_FLOAT_KIND_NONE;
}

int ps_lvar_vla_row_stride_frame_off(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_pointer_view_vla_row_stride_frame_off(type);
}

int ps_lvar_vla_strides_remaining(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_pointer_view_vla_strides_remaining(type);
}

int ps_lvar_vla_row_stride_elem_size(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_vla_row_stride_elem_size(type);
}

int ps_lvar_vla_row_stride_src_offset(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_vla_row_stride_src_offset(type);
}

int ps_lvar_vla_param_inner_dim_count(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_vla_param_inner_dim_count(type);
}

int ps_lvar_vla_param_inner_dim_const(const lvar_t *var, int idx) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_vla_param_inner_dim_const(type, idx);
}

int ps_lvar_vla_param_inner_dim_src_offset(const lvar_t *var, int idx) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_vla_param_inner_dim_src_offset(type, idx);
}

void ps_decl_set_gvar_decl_type(global_var_t *gv,
                                 const psx_type_t *decl_type) {
  if (!gv || !decl_type) return;
  gv->decl_type = ps_type_clone_persistent(decl_type);
}

void psx_decl_reset_translation_unit_state(void) {
  psx_decl_reset_locals();
  current_funcname = NULL;
  current_funcname_len = 0;
  psx_declaration_pipeline_reset_translation_unit_state();
}

/* 集合体メンバ情報は semantic_ctx 側の統合 API (tag_member_info_t) を
 * そのまま再利用する (Phase A1 リファクタリング)。 */

unsigned char psx_funcptr_ret_int_width_from_kind(token_kind_t kind, int is_pointer,
                                                  tk_float_kind_t fp_kind) {
  if (is_pointer || fp_kind != TK_FLOAT_KIND_NONE || kind == TK_VOID ||
      ps_ctx_is_tag_aggregate_kind(kind) || kind == TK_EOF) {
    return 0;
  }
  return ps_ctx_scalar_type_size(kind) >= 8 ? 8 : 4;
}

int psx_funcptr_signature_has_payload(psx_funcptr_signature_t sig) {
  return sig.param_fp_mask || sig.param_int_mask || sig.is_variadic ||
         sig.nargs_fixed;
}

int psx_funcptr_return_shape_has_payload(psx_funcptr_return_shape_t ret) {
  return ret.int_width ||
         ret.fp_kind != TK_FLOAT_KIND_NONE ||
         ret.pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         ret.is_void || ret.is_data_pointer || ret.is_complex ||
         psx_ret_pointee_array_has_dims(ret.pointee_array);
}

int psx_funcptr_return_shape_matches(psx_funcptr_return_shape_t a,
                                     psx_funcptr_return_shape_t b) {
  return a.int_width == b.int_width &&
         a.fp_kind == b.fp_kind &&
         a.pointee_fp_kind == b.pointee_fp_kind &&
         a.is_void == b.is_void &&
         a.is_data_pointer == b.is_data_pointer &&
         a.is_complex == b.is_complex &&
         psx_ret_pointee_array_equal(a.pointee_array, b.pointee_array);
}

psx_funcptr_return_shape_t psx_decl_funcptr_direct_return_shape(
    psx_decl_funcptr_sig_t sig) {
  return sig.function.callable.return_shape;
}

psx_funcptr_return_shape_t psx_funcptr_return_shape_merge_missing(
    psx_funcptr_return_shape_t merged, psx_funcptr_return_shape_t src) {
  if (!merged.int_width && src.int_width) merged.int_width = src.int_width;
  if (merged.fp_kind == TK_FLOAT_KIND_NONE &&
      src.fp_kind != TK_FLOAT_KIND_NONE) {
    merged.fp_kind = src.fp_kind;
  }
  if (merged.pointee_fp_kind == TK_FLOAT_KIND_NONE &&
      src.pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    merged.pointee_fp_kind = src.pointee_fp_kind;
  }
  if (src.is_void) merged.is_void = 1;
  if (src.is_data_pointer) merged.is_data_pointer = 1;
  if (src.is_complex) merged.is_complex = 1;
  if (!psx_ret_pointee_array_has_dims(merged.pointee_array) &&
      psx_ret_pointee_array_has_dims(src.pointee_array)) {
    merged.pointee_array = src.pointee_array;
  }
  return merged;
}

int psx_funcptr_callable_shape_has_payload(psx_funcptr_callable_shape_t fn) {
  return psx_funcptr_signature_has_payload(fn.signature) ||
         psx_funcptr_return_shape_has_payload(fn.return_shape);
}

int psx_funcptr_callable_shape_matches(psx_funcptr_callable_shape_t a,
                                       psx_funcptr_callable_shape_t b) {
  return a.signature.param_fp_mask == b.signature.param_fp_mask &&
         a.signature.param_int_mask == b.signature.param_int_mask &&
         a.signature.is_variadic == b.signature.is_variadic &&
         (!a.signature.is_variadic ||
          a.signature.nargs_fixed == b.signature.nargs_fixed) &&
         (a.signature.nargs_fixed <= 0 || b.signature.nargs_fixed <= 0 ||
          a.signature.nargs_fixed == b.signature.nargs_fixed) &&
         psx_funcptr_return_shape_matches(a.return_shape, b.return_shape);
}

psx_funcptr_callable_shape_t psx_funcptr_callable_shape_merge_missing(
    psx_funcptr_callable_shape_t merged, psx_funcptr_callable_shape_t src,
    int copy_variadic) {
  if (!merged.signature.param_fp_mask && src.signature.param_fp_mask)
    merged.signature.param_fp_mask = src.signature.param_fp_mask;
  if (!merged.signature.param_int_mask && src.signature.param_int_mask)
    merged.signature.param_int_mask = src.signature.param_int_mask;
  if (copy_variadic && src.signature.is_variadic)
    merged.signature.is_variadic = 1;
  if (!merged.signature.nargs_fixed && src.signature.nargs_fixed)
    merged.signature.nargs_fixed = src.signature.nargs_fixed;
  merged.return_shape =
      psx_funcptr_return_shape_merge_missing(merged.return_shape,
                                             src.return_shape);
  return merged;
}

int ps_funcptr_returned_func_has_payload(psx_funcptr_returned_func_t ret) {
  return ret.is_funcptr ||
         (ret.type && psx_funcptr_type_shape_has_payload(*ret.type));
}

static psx_funcptr_type_shape_t *psx_funcptr_type_shape_clone_heap(
    psx_funcptr_type_shape_t fn);

psx_funcptr_type_shape_t ps_funcptr_returned_func_as_type_shape(
    psx_funcptr_returned_func_t ret) {
  return ret.type ? *ret.type : (psx_funcptr_type_shape_t){0};
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_from_type_shape(
    psx_funcptr_type_shape_t fn) {
  psx_funcptr_returned_func_t ret = {0};
  ret.is_funcptr = psx_funcptr_type_shape_has_payload(fn) ? 1 : 0;
  if (ret.is_funcptr) ret.type = psx_funcptr_type_shape_clone_heap(fn);
  return ret;
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_mark(
    psx_funcptr_returned_func_t ret) {
  ret.is_funcptr = 1;
  return ret;
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_clone(
    psx_funcptr_returned_func_t ret) {
  psx_funcptr_returned_func_t copy = {0};
  copy.is_funcptr = ret.is_funcptr;
  if (ret.type) copy.type = psx_funcptr_type_shape_clone_heap(*ret.type);
  return copy;
}

int psx_funcptr_returned_func_matches(psx_funcptr_returned_func_t a,
                                      psx_funcptr_returned_func_t b) {
  psx_funcptr_type_shape_t a_fn = ps_funcptr_returned_func_as_type_shape(a);
  psx_funcptr_type_shape_t b_fn = ps_funcptr_returned_func_as_type_shape(b);
  return a.is_funcptr == b.is_funcptr &&
         psx_funcptr_type_shape_matches(a_fn, b_fn);
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_merge_missing(
    psx_funcptr_returned_func_t merged, psx_funcptr_returned_func_t src,
    int copy_variadic) {
  int merged_is_funcptr = merged.is_funcptr || src.is_funcptr;
  psx_funcptr_type_shape_t merged_fn =
      ps_funcptr_returned_func_as_type_shape(merged);
  psx_funcptr_type_shape_t src_fn =
      ps_funcptr_returned_func_as_type_shape(src);
  merged_fn = psx_funcptr_type_shape_merge_missing(
      merged_fn, src_fn, copy_variadic);
  merged = psx_funcptr_returned_func_from_type_shape(merged_fn);
  if (merged_is_funcptr) merged = psx_funcptr_returned_func_mark(merged);
  return merged;
}

static psx_funcptr_type_shape_t *psx_funcptr_type_shape_clone_heap(
    psx_funcptr_type_shape_t fn) {
  psx_funcptr_type_shape_t *copy = calloc(1, sizeof(*copy));
  if (!copy) return NULL;
  *copy = psx_funcptr_type_shape_clone(fn);
  return copy;
}

int psx_funcptr_type_shape_has_payload(psx_funcptr_type_shape_t fn) {
  return psx_funcptr_callable_shape_has_payload(fn.callable) ||
         ps_funcptr_returned_func_has_payload(fn.returned_funcptr);
}

int psx_funcptr_type_shape_matches(psx_funcptr_type_shape_t a,
                                   psx_funcptr_type_shape_t b) {
  int has_returned =
      ps_funcptr_returned_func_has_payload(a.returned_funcptr) ||
      ps_funcptr_returned_func_has_payload(b.returned_funcptr);
  return psx_funcptr_callable_shape_matches(a.callable, b.callable) &&
         (!has_returned ||
          psx_funcptr_returned_func_matches(a.returned_funcptr,
                                            b.returned_funcptr));
}

psx_funcptr_type_shape_t psx_funcptr_type_shape_merge_missing(
    psx_funcptr_type_shape_t merged, psx_funcptr_type_shape_t src,
    int copy_variadic) {
  merged.callable = psx_funcptr_callable_shape_merge_missing(
      merged.callable, src.callable, copy_variadic);
  if (ps_funcptr_returned_func_has_payload(merged.returned_funcptr) ||
      ps_funcptr_returned_func_has_payload(src.returned_funcptr)) {
    merged.returned_funcptr = psx_funcptr_returned_func_merge_missing(
        merged.returned_funcptr, src.returned_funcptr, copy_variadic);
  }
  return merged;
}

psx_funcptr_type_shape_t psx_funcptr_type_shape_clone(
    psx_funcptr_type_shape_t fn) {
  psx_funcptr_type_shape_t copy = {0};
  copy.callable = fn.callable;
  copy.returned_funcptr =
      psx_funcptr_returned_func_clone(fn.returned_funcptr);
  return copy;
}

psx_decl_funcptr_sig_t ps_decl_funcptr_sig_clone(psx_decl_funcptr_sig_t sig) {
  psx_decl_funcptr_sig_t copy = {0};
  copy.function = psx_funcptr_type_shape_clone(sig.function);
  return copy;
}

int ps_decl_funcptr_sig_has_payload(psx_decl_funcptr_sig_t sig) {
  return psx_funcptr_type_shape_has_payload(sig.function);
}

psx_decl_funcptr_sig_t psx_decl_make_funcptr_sig(const psx_funcptr_signature_t *suffix_sig,
                                                 unsigned char ret_int_width,
                                                 tk_float_kind_t ret_fp_kind,
                                                 psx_ret_pointee_array_t ret_pointee_array,
                                                 int ret_is_void,
                                                 int ret_is_data_pointer,
                                                 int ret_is_funcptr,
                                                 int ret_is_complex) {
  psx_decl_funcptr_sig_t sig = {0};
  if (suffix_sig) {
    sig.function.callable.signature = *suffix_sig;
  }
  sig.function.callable.return_shape.int_width = ret_int_width;
  sig.function.callable.return_shape.pointee_array = ret_pointee_array;
  int ret_is_pointer_like =
      ret_is_data_pointer || psx_ret_pointee_array_has_dims(ret_pointee_array);
  sig.function.callable.return_shape.fp_kind =
      ret_is_pointer_like ? TK_FLOAT_KIND_NONE : ret_fp_kind;
  sig.function.callable.return_shape.pointee_fp_kind =
      ret_is_pointer_like ? ret_fp_kind : TK_FLOAT_KIND_NONE;
  sig.function.callable.return_shape.is_void =
      (ret_is_void && !ret_is_data_pointer) ? 1 : 0;
  sig.function.callable.return_shape.is_data_pointer = ret_is_data_pointer ? 1 : 0;
  if (ret_is_funcptr) {
    sig.function.returned_funcptr =
        psx_funcptr_returned_func_mark(sig.function.returned_funcptr);
  }
  sig.function.callable.return_shape.is_complex =
      (ret_is_complex && !ret_is_data_pointer) ? 1 : 0;
  return sig;
}

psx_decl_funcptr_sig_t ps_decl_make_funcptr_sig_from_kind(
    const psx_funcptr_signature_t *suffix_sig, token_kind_t ret_kind,
    tk_float_kind_t fp_kind, int ret_is_data_pointer, int ret_is_funcptr,
    int ret_is_complex, psx_ret_pointee_array_t ret_pointee_array) {
  return psx_decl_make_funcptr_sig(
      suffix_sig,
      psx_funcptr_ret_int_width_from_kind(ret_kind, ret_is_data_pointer, fp_kind),
      fp_kind, ret_pointee_array, ret_kind == TK_VOID, ret_is_data_pointer,
      ret_is_funcptr, ret_is_complex);
}

void ps_decl_funcptr_sig_promote_return_to_funcptr(
    psx_decl_funcptr_sig_t *sig, const psx_funcptr_signature_t *returned_sig) {
  if (!sig) return;
  psx_funcptr_type_shape_t returned_fn = {0};
  if (returned_sig) returned_fn.callable.signature = *returned_sig;
  returned_fn.callable.return_shape = psx_decl_funcptr_direct_return_shape(*sig);
  sig->function.returned_funcptr =
      psx_funcptr_returned_func_from_type_shape(returned_fn);
  sig->function.returned_funcptr =
      psx_funcptr_returned_func_mark(sig->function.returned_funcptr);
  sig->function.callable.return_shape = (psx_funcptr_return_shape_t){0};
}

void ps_decl_set_gvar_type_size(global_var_t *gv, int type_size) {
  if (!gv) return;
  psx_type_t *type = ps_gvar_get_decl_type(gv);
  gv->type_size = type_size;
  if (!type || type_size < 0) return;
  if (type->kind == PSX_TYPE_ARRAY) {
    int elem_size = type->elem_size;
    if (elem_size <= 0 && type->base)
      elem_size = ps_type_sizeof(type->base);
    type->size = type_size;
    if (elem_size > 0 && type_size % elem_size == 0)
      type->array_len = type_size / elem_size;
  } else if (type->kind != PSX_TYPE_POINTER) {
    type->size = type_size;
    type->align = type_size >= 8 ? 8
                                 : (type_size >= 4 ? 4
                                                   : (type_size >= 2 ? 2 : 1));
  }
}

node_t *ps_decl_bind_initializer_for_var(
    lvar_t *var, int is_pointer, node_t *initializer,
    psx_decl_init_kind_t initializer_kind, token_t *init_tok) {
  node_t *target =
      ps_lvar_is_array(var) || ps_lvar_is_tag_aggregate(var)
          ? ps_node_new_lvar_object_ref_for(var)
          : ps_node_new_lvar_expr_ref_for(var, is_pointer);
  return ps_node_new_raw_decl_initializer(
      target, initializer, initializer_kind, init_tok);
}

node_t *ps_decl_parse_initializer_for_var(lvar_t *var, int is_pointer) {
  if (curtok() && curtok()->kind == TK_LBRACE) {
    token_t *init_tok = curtok();
    node_t *syntax = psx_parse_initializer_syntax_list();
    return ps_decl_bind_initializer_for_var(
        var, is_pointer, syntax, PSX_DECL_INIT_LIST, init_tok);
  }
  token_t *init_tok = curtok();
  return ps_decl_bind_initializer_for_var(
      var, is_pointer, ps_expr_assign(), PSX_DECL_INIT_EXPR, init_tok);
}
