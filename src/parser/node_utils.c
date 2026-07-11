#include "node_utils.h"
#include "decl.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "arena.h"
#include "diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline token_t *curtok(void) { return tk_get_current_token(); }
static int type_is_pointer_view_type(const psx_type_t *type);
static psx_type_t *type_new_void(void);
static psx_type_t *lvar_decl_type_view(const lvar_t *var);
static psx_type_t *gvar_decl_type_view(const global_var_t *gv);

typedef enum {
  NODE_SCALAR_UNSIGNED,
  NODE_SCALAR_LONG_LONG,
  NODE_SCALAR_PLAIN_CHAR,
  NODE_SCALAR_LONG_DOUBLE,
} node_scalar_flag_t;

typedef enum {
  NODE_POINTEE_UNSIGNED,
  NODE_POINTEE_BOOL,
  NODE_POINTEE_VOID,
  NODE_POINTEE_CONST,
  NODE_POINTEE_VOLATILE,
} node_pointee_flag_t;

typedef enum {
  NODE_POINTER_QUAL_LEVELS,
  NODE_POINTER_BASE_DEREF_SIZE,
  NODE_POINTER_PTR_ARRAY_POINTEE_BYTES,
  NODE_POINTER_CONST_MASK,
  NODE_POINTER_VOLATILE_MASK,
  NODE_POINTER_POINTEE_FP_KIND,
} node_pointer_view_field_t;

typedef enum {
  NODE_VALUE_TYPE_SIZE,
  NODE_VALUE_DEREF_SIZE,
  NODE_VALUE_IS_POINTER,
} node_value_view_field_t;

typedef enum {
  NODE_VLA_ROW_STRIDE_FRAME_OFF,
  NODE_VLA_STRIDES_REMAINING,
} node_vla_view_field_t;

typedef struct {
  token_kind_t kind;
  char *name;
  int len;
  int is_pointer;
  int scope_depth_p1;
} node_tag_view_t;

typedef struct {
  int type_size;
  int deref_size;
  int base_deref_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  int is_tag_pointer;
  int is_pointer;
  int is_unsigned;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_pointer_const_qualified;
  int is_pointer_volatile_qualified;
  int is_complex;
  int is_atomic;
  int pointee_is_void;
  int is_bool;
  int is_long_long;
  int is_plain_char;
  int is_long_double;
  int pointee_is_bool;
  int pointee_is_unsigned;
  int pointee_is_scalar_ptr;
  int is_scalar_ptr_member;
  int is_array_member;
  tk_float_kind_t pointee_fp_kind;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;
  int pointer_qual_levels;
  int inner_deref_size;
  int next_deref_size;
  int extra_strides[5];
  int extra_strides_count;
  int vla_row_stride_frame_off;
  int vla_strides_remaining;
  int ptr_array_pointee_bytes;
  int compound_literal_array_size;
  psx_decl_funcptr_sig_t funcptr_sig;
} legacy_decl_shape_t;

static int pointer_view_from_node_direct(node_t *node, node_pointer_view_field_t field,
                                         int *value);
static int node_pointer_stride_from_type_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride, int *inner_stride,
    int *next_stride, int *extra_strides, int *extra_strides_count);
static int node_value_view_from_node_direct(node_t *node, node_value_view_field_t field,
                                            int *value);
static int node_self_is_const_qualified(node_t *node);
static int node_self_is_volatile_qualified(node_t *node);
static psx_type_t *type_decay_array_to_pointer(psx_type_t *array_type);
static const psx_type_t *type_pointee_value_type(const psx_type_t *type);
static void gvar_tag_identity(const global_var_t *gv, token_kind_t *kind,
                              char **name, int *len, int *scope_depth_p1);
static int node_scalar_ptr_member_lvalue(node_t *node);

static int is_lvalue_clone_kind(node_kind_t kind) {
  return kind == ND_LVAR || kind == ND_GVAR || kind == ND_DEREF ||
         kind == ND_STRING;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_legacy_decl_shape(const legacy_decl_shape_t *mem) {
  if (!mem) return (psx_decl_funcptr_sig_t){0};
  return psx_decl_funcptr_sig_clone(mem->funcptr_sig);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_type(const psx_type_t *type) {
  if (!type) return (psx_decl_funcptr_sig_t){0};
  return psx_decl_funcptr_sig_clone(type->funcptr_sig);
}

static int funcptr_sig_has_return_shape(psx_decl_funcptr_sig_t sig) {
  return psx_funcptr_return_shape_has_payload(
             psx_decl_funcptr_direct_return_shape(sig)) ||
         psx_funcptr_returned_func_has_payload(sig.function.returned_funcptr);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_lvar(const lvar_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  return funcptr_sig_from_type(lvar_decl_type_view(src));
}

static psx_decl_funcptr_sig_t funcptr_sig_from_gvar(const global_var_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  return funcptr_sig_from_type(gvar_decl_type_view(src));
}

static psx_decl_funcptr_sig_t funcptr_sig_merge_missing(psx_decl_funcptr_sig_t merged,
                                                        const psx_decl_funcptr_sig_t *sig,
                                                        int copy_variadic) {
  if (!sig) return merged;
  merged.function = psx_funcptr_type_shape_merge_missing(
      merged.function, sig->function, copy_variadic);
  return merged;
}

static void legacy_decl_shape_store_funcptr_signature(legacy_decl_shape_t *dst,
                                             const psx_decl_funcptr_sig_t *sig) {
  if (!dst || !sig) return;
  dst->funcptr_sig = psx_decl_funcptr_sig_clone(*sig);
}

psx_decl_funcptr_sig_t psx_node_funcdef_ret_funcptr_sig(const node_func_t *fn) {
  if (!fn) return (psx_decl_funcptr_sig_t){0};
  if (fn->base.type && psx_decl_funcptr_sig_has_payload(fn->base.type->funcptr_sig))
    return psx_decl_funcptr_sig_clone(fn->base.type->funcptr_sig);
  return psx_decl_funcptr_sig_clone(fn->ret_funcptr_sig);
}

void psx_node_funcdef_set_ret_funcptr_sig(node_func_t *fn, psx_decl_funcptr_sig_t sig) {
  if (!fn) return;
  fn->ret_funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  if (psx_decl_funcptr_sig_has_payload(sig)) {
    fn->base.type = psx_type_new_pointer(NULL, 8);
    fn->base.type->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  } else {
    fn->base.type = NULL;
  }
}

static void legacy_decl_shape_copy_funcptr_metadata(psx_type_t *type,
                                             const legacy_decl_shape_t *mem) {
  if (!type || !mem) return;
  type->funcptr_sig = funcptr_sig_from_legacy_decl_shape(mem);
}

static int tag_scope_depth_from_p1(int scope_depth_p1) {
  return scope_depth_p1 > 0 ? scope_depth_p1 - 1 : -1;
}

static int ctx_get_tag_member_count_scoped(token_kind_t tk, char *tn, int tl,
                                           int scope_depth_p1) {
  int scope_depth = tag_scope_depth_from_p1(scope_depth_p1);
  if (scope_depth >= 0) {
    int n = psx_ctx_get_tag_member_count_at_scope(tk, tn, tl, scope_depth);
    if (n >= 0) return n;
  }
  return psx_ctx_get_tag_member_count(tk, tn, tl);
}

static int ctx_get_tag_member_info_scoped(token_kind_t tk, char *tn, int tl,
                                          int scope_depth_p1, int idx,
                                          tag_member_info_t *out) {
  int scope_depth = tag_scope_depth_from_p1(scope_depth_p1);
  if (scope_depth >= 0) {
    return psx_ctx_get_tag_member_info_at_scope(tk, tn, tl, scope_depth, idx, out);
  }
  return psx_ctx_get_tag_member_info(tk, tn, tl, idx, out);
}

static int tag_aggregate_size_from_ctx(token_kind_t tk, char *tn, int tl,
                                       int scope_depth_p1, int fallback) {
  if (fallback > 0) return fallback;
  int n = ctx_get_tag_member_count_scoped(tk, tn, tl, scope_depth_p1);
  int max_end = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!ctx_get_tag_member_info_scoped(tk, tn, tl, scope_depth_p1, i, &mi)) break;
    int end = mi.offset + psx_tag_member_decl_storage_size(&mi);
    if (end > max_end) max_end = end;
  }
  int align = psx_ctx_get_tag_align(tk, tn, tl);
  if (align > 1 && max_end > 0) max_end = (max_end + align - 1) / align * align;
  return max_end > 0 ? max_end : fallback;
}

/* Reconstruct canonical declaration types at the legacy symbol boundary. */
static int legacy_decl_shape_tag_pointee_size(const legacy_decl_shape_t *mem) {
  if (!mem || !psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) return 0;
  int fallback = mem->deref_size > 0 ? mem->deref_size : mem->base_deref_size;
  return tag_aggregate_size_from_ctx(mem->tag_kind, mem->tag_name, mem->tag_len,
                                     mem->tag_scope_depth_p1, fallback);
}

static int legacy_decl_shape_pointee_scalar_is_bool(const legacy_decl_shape_t *mem) {
  return mem && mem->pointee_is_bool ? 1 : 0;
}

static int legacy_decl_shape_array_element_flag(const legacy_decl_shape_t *mem, int scalar_flag) {
  if (!mem || !scalar_flag || mem->is_scalar_ptr_member) return 0;
  if (mem->is_pointer && mem->pointer_qual_levels > 0) return 0;
  if (mem->is_array_member) return 1;
  return mem->type_size > 0 && mem->deref_size > 0 &&
         mem->type_size >= mem->deref_size &&
         (mem->type_size % mem->deref_size) == 0;
}

static int legacy_decl_shape_array_leaf_is_bool(const legacy_decl_shape_t *mem) {
  return mem && (mem->pointee_is_bool ||
                 legacy_decl_shape_array_element_flag(mem, mem->is_bool)) ? 1 : 0;
}

static int legacy_decl_shape_array_leaf_is_unsigned(const legacy_decl_shape_t *mem) {
  return mem && (mem->pointee_is_unsigned ||
                 legacy_decl_shape_array_element_flag(mem, mem->is_unsigned)) ? 1 : 0;
}

static int legacy_decl_shape_has_pointee_payload(const legacy_decl_shape_t *mem) {
  return mem &&
         (mem->pointer_qual_levels > 0 || mem->pointee_is_scalar_ptr ||
          mem->pointee_is_void || mem->pointee_is_bool ||
          mem->pointee_is_unsigned || mem->is_pointer_const_qualified ||
          mem->is_pointer_volatile_qualified ||
          mem->pointer_const_qual_mask != 0 ||
          mem->pointer_volatile_qual_mask != 0 ||
          mem->pointee_fp_kind != TK_FLOAT_KIND_NONE ||
          mem->ptr_array_pointee_bytes > 0);
}

static psx_type_t *legacy_decl_shape_reconstruct_pointee_base(const legacy_decl_shape_t *mem) {
  if (!mem) return NULL;
  if (mem->pointee_is_scalar_ptr) {
    int base_size = mem->base_deref_size > 0 ? mem->base_deref_size : 4;
    psx_type_t *base = mem->pointee_fp_kind != TK_FLOAT_KIND_NONE
                           ? psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind,
                                                base_size)
                           : psx_type_new_integer(legacy_decl_shape_pointee_scalar_is_bool(mem)
                                                      ? TK_BOOL : TK_EOF,
                                                   base_size,
                                                   mem->pointee_is_unsigned);
    psx_type_t *ptr = psx_type_new_pointer(base, base_size);
    ptr->base_deref_size = base_size;
    ptr->pointer_qual_levels = 1;
    return ptr;
  }
  if (mem->pointee_is_void) return type_new_void();
  if (psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) {
    int size = legacy_decl_shape_tag_pointee_size(mem);
    return psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                            mem->tag_scope_depth_p1, size);
  }
  if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    int sz = mem->base_deref_size > 0 ? mem->base_deref_size : mem->deref_size;
    if (sz <= 0) sz = 8;
    return psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind, sz);
  }
  int sz = mem->base_deref_size > 0 ? mem->base_deref_size : mem->deref_size;
  if (sz <= 0 || sz > 8) sz = 4;
  return psx_type_new_integer(legacy_decl_shape_pointee_scalar_is_bool(mem) ? TK_BOOL : TK_EOF, sz,
                              mem->pointee_is_unsigned);
}

static psx_type_t *legacy_decl_shape_reconstruct_scalar_pointee(const legacy_decl_shape_t *mem,
                                                         int scalar_size) {
  if (!mem) return NULL;
  if (scalar_size <= 0) scalar_size = mem->base_deref_size;
  if (scalar_size <= 0) scalar_size = mem->deref_size;
  if (scalar_size <= 0) scalar_size = mem->pointee_is_bool ? 1 : 4;
  if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    return psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind,
                              scalar_size);
  }
  if (psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) {
    int tag_size = legacy_decl_shape_tag_pointee_size(mem);
    if (tag_size <= 0) tag_size = scalar_size;
    return psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                            mem->tag_scope_depth_p1, tag_size);
  }
  return psx_type_new_integer(legacy_decl_shape_pointee_scalar_is_bool(mem) ? TK_BOOL : TK_EOF,
                              scalar_size, mem->pointee_is_unsigned);
}

static psx_type_t *legacy_decl_shape_reconstruct_array_base(const legacy_decl_shape_t *mem) {
  if (mem && mem->ptr_array_pointee_bytes > 0 && mem->base_deref_size > 0 &&
      mem->pointee_is_scalar_ptr) {
    int scalar_size = mem->base_deref_size;
    psx_type_t *scalar = mem->pointee_fp_kind != TK_FLOAT_KIND_NONE
                             ? psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind,
                                                  scalar_size)
                             : psx_type_new_integer(legacy_decl_shape_pointee_scalar_is_bool(mem)
                                                        ? TK_BOOL : TK_EOF,
                                                     scalar_size,
                                                     mem->pointee_is_unsigned);
    int row_len = mem->ptr_array_pointee_bytes / scalar_size;
    if (row_len <= 0) row_len = 1;
    psx_type_t *row = psx_type_new_array(scalar, row_len,
                                         mem->ptr_array_pointee_bytes,
                                         scalar_size, 0);
    row->base_deref_size = scalar_size;
    psx_type_t *ptr = psx_type_new_pointer(row, mem->ptr_array_pointee_bytes);
    ptr->base_deref_size = scalar_size;
    ptr->pointer_qual_levels = mem->pointer_qual_levels > 0
                                   ? mem->pointer_qual_levels
                                   : 1;
    ptr->outer_stride = mem->ptr_array_pointee_bytes;
    ptr->mid_stride = 0;
    return ptr;
  }
  if (mem && mem->is_tag_pointer && psx_ctx_is_tag_aggregate_kind(mem->tag_kind) &&
      (mem->ptr_array_pointee_bytes <= 0 ||
       (mem->pointee_is_scalar_ptr && mem->pointer_qual_levels > 0))) {
    int tag_size = legacy_decl_shape_tag_pointee_size(mem);
    psx_type_t *tag = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                                       mem->tag_scope_depth_p1, tag_size);
    int levels = mem->pointer_qual_levels > 0 ? mem->pointer_qual_levels : 1;
    int top_deref = levels >= 2 ? 8 : tag_size;
    return psx_type_wrap_pointer_levels(tag, levels, top_deref, tag_size,
                                        mem->pointer_const_qual_mask,
                                        mem->pointer_volatile_qual_mask);
  }
  if (mem && psx_ctx_is_tag_aggregate_kind(mem->tag_kind) &&
      mem->pointer_qual_levels > 0) {
    int tag_size = legacy_decl_shape_tag_pointee_size(mem);
    psx_type_t *tag = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                                       mem->tag_scope_depth_p1, tag_size);
    int top_deref = mem->pointer_qual_levels >= 2 ? 8 : tag_size;
    return psx_type_wrap_pointer_levels(tag, mem->pointer_qual_levels,
                                        top_deref, tag_size,
                                        mem->pointer_const_qual_mask,
                                        mem->pointer_volatile_qual_mask);
  }
  if ((legacy_decl_shape_array_leaf_is_bool(mem) ||
       legacy_decl_shape_array_leaf_is_unsigned(mem)) &&
      mem->pointee_fp_kind == TK_FLOAT_KIND_NONE &&
      !psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) {
    int sz = mem->base_deref_size > 0 ? mem->base_deref_size : mem->deref_size;
    if (sz <= 0 || sz > 8) sz = 1;
    return psx_type_new_integer(legacy_decl_shape_array_leaf_is_bool(mem) ? TK_BOOL : TK_EOF,
                                sz, legacy_decl_shape_array_leaf_is_unsigned(mem));
  }
  return legacy_decl_shape_reconstruct_pointee_base(mem);
}

static psx_type_t *legacy_decl_shape_reconstruct_array_shape(const legacy_decl_shape_t *mem,
                                                      int elem_size,
                                                      int force_vla) {
  if (!mem || mem->type_size <= 0 || elem_size <= 0) return NULL;

  int strides[8] = {0};
  int count = 0;
  strides[count++] = mem->type_size;
  int candidates[8] = {
    mem->inner_deref_size,
    mem->deref_size,
    mem->next_deref_size,
    mem->extra_strides[0],
    mem->extra_strides[1],
    mem->extra_strides[2],
    mem->extra_strides[3],
    mem->extra_strides[4],
  };
  for (int i = 0; i < 8 && count < 8; i++) {
    int best = 0;
    for (int j = 0; j < 8; j++) {
      int stride = candidates[j];
      if (stride > 0 && stride < strides[count - 1] && stride > best)
        best = stride;
    }
    if (best <= 0) break;
    strides[count++] = best;
    for (int j = 0; j < 8; j++) {
      if (candidates[j] == best) candidates[j] = 0;
    }
  }
  psx_type_t *type = legacy_decl_shape_reconstruct_array_base(mem);
  int leaf_storage_size = psx_type_sizeof(type);
  if (leaf_storage_size <= 0) leaf_storage_size = elem_size;
  if (strides[count - 1] != leaf_storage_size && count < 8) {
    if (leaf_storage_size < strides[count - 1]) strides[count++] = leaf_storage_size;
  }
  if (count < 2) return NULL;

  int leaf_size = elem_size;
  for (int i = count - 2; i >= 0; i--) {
    int total_size = strides[i];
    int child_size = strides[i + 1];
    if (total_size <= 0 || child_size <= 0) return NULL;
    int array_len = total_size / child_size;
    if (array_len <= 0) array_len = 1;
    psx_type_t *array = psx_type_new_array(type, array_len, total_size,
                                           child_size, force_vla);
    array->base_deref_size = leaf_size;
    array->outer_stride = child_size;
    array->mid_stride = (i + 2 < count) ? strides[i + 2] : 0;
    int extra_count = 0;
    for (int j = i + 3; j < count && extra_count < 5; j++)
      array->extra_strides[extra_count++] = strides[j];
    array->extra_strides_count = (unsigned char)extra_count;
    type = array;
  }
  return type;
}

static int type_pointer_depth(const psx_type_t *type) {
  int depth = 0;
  while (type && type->kind == PSX_TYPE_POINTER) {
    depth++;
    type = type->base;
  }
  return depth;
}

static psx_type_t *legacy_decl_shape_reconstruct_inner_pointer_levels(
    psx_type_t *base, const legacy_decl_shape_t *mem) {
  if (!base || !mem || mem->pointer_qual_levels <= 1) return base;
  int base_deref = mem->base_deref_size > 0 ? mem->base_deref_size : mem->deref_size;
  if (base_deref <= 0) base_deref = psx_type_sizeof(base);
  if (base_deref <= 0) base_deref = 4;
  int depth = type_pointer_depth(base);
  int wanted_depth = mem->pointer_qual_levels - 1;
  if (depth >= wanted_depth) return base;
  int missing_levels = wanted_depth - depth;
  int top_deref = missing_levels >= 2 ? 8 : base_deref;
  psx_type_t *wrapped = psx_type_wrap_pointer_levels(
      base, missing_levels, top_deref, base_deref,
      mem->pointer_const_qual_mask >> 1,
      mem->pointer_volatile_qual_mask >> 1);
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_legacy_decl_shape(mem);
  for (psx_type_t *cur = wrapped; cur && cur->kind == PSX_TYPE_POINTER; cur = cur->base) {
    cur->pointee_fp_kind = (tk_float_kind_t)mem->pointee_fp_kind;
    cur->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  }
  return wrapped;
}

static psx_type_t *legacy_decl_shape_reconstruct_pointer_array_base(
    const legacy_decl_shape_t *mem) {
  if (!mem || mem->inner_deref_size <= 0 || mem->ptr_array_pointee_bytes <= 0 ||
      mem->pointer_qual_levels < 2) {
    return NULL;
  }
  int scalar_size = mem->base_deref_size > 0 ? mem->base_deref_size
                                             : mem->deref_size;
  if (scalar_size <= 0) scalar_size = mem->pointee_is_bool ? 1 : 4;
  psx_type_t *scalar = legacy_decl_shape_reconstruct_scalar_pointee(mem, scalar_size);
  int row_len = scalar_size > 0 ? mem->ptr_array_pointee_bytes / scalar_size : 0;
  if (row_len <= 0) row_len = 1;
  psx_type_t *row = psx_type_new_array(scalar, row_len,
                                       mem->ptr_array_pointee_bytes,
                                       scalar_size, 0);
  psx_type_t *row_ptr = psx_type_new_pointer(row, mem->ptr_array_pointee_bytes);
  row_ptr->base_deref_size = scalar_size;
  row_ptr->pointer_qual_levels = 1;
  row_ptr->outer_stride = mem->ptr_array_pointee_bytes;
  int elem_size = 8;
  int array_len = mem->inner_deref_size / elem_size;
  if (array_len <= 0) array_len = 1;
  psx_type_t *array = psx_type_new_array(row_ptr, array_len,
                                         mem->inner_deref_size,
                                         elem_size, 0);
  array->base_deref_size = scalar_size;
  array->pointer_qual_levels = 1;
  array->ptr_array_pointee_bytes = mem->ptr_array_pointee_bytes;
  array->outer_stride = mem->inner_deref_size;
  return array;
}

static psx_type_t *legacy_decl_shape_reconstruct_pointer_element_array_shape(
    const legacy_decl_shape_t *mem, psx_type_t *elem_ptr, int elem_store_size,
    int scalar_size) {
  if (!mem || !elem_ptr || mem->ptr_array_pointee_bytes <= 0 ||
      elem_store_size <= 0) {
    return NULL;
  }
  int strides[8] = {0};
  int count = 0;
  strides[count++] = mem->ptr_array_pointee_bytes;

  int candidates[7] = {
    mem->next_deref_size,
    mem->extra_strides[0],
    mem->extra_strides[1],
    mem->extra_strides[2],
    mem->extra_strides[3],
    mem->extra_strides[4],
    elem_store_size,
  };
  while (count < 8) {
    int best = 0;
    for (int i = 0; i < 7; i++) {
      int stride = candidates[i];
      if (stride > 0 && stride < strides[count - 1] && stride > best)
        best = stride;
    }
    if (best <= 0) break;
    strides[count++] = best;
    for (int i = 0; i < 7; i++) {
      if (candidates[i] == best) candidates[i] = 0;
    }
  }
  if (strides[count - 1] != elem_store_size && count < 8 &&
      elem_store_size < strides[count - 1]) {
    strides[count++] = elem_store_size;
  }
  if (count < 2) return NULL;

  psx_type_t *type = elem_ptr;
  for (int i = count - 2; i >= 0; i--) {
    int total_size = strides[i];
    int child_size = strides[i + 1];
    if (total_size <= 0 || child_size <= 0) return NULL;
    int array_len = total_size / child_size;
    if (array_len <= 0) array_len = 1;
    psx_type_t *array = psx_type_new_array(type, array_len, total_size,
                                           child_size, 0);
    array->base_deref_size = scalar_size;
    array->outer_stride = child_size;
    array->mid_stride = (i + 2 < count) ? strides[i + 2] : 0;
    int extra_count = 0;
    for (int j = i + 3; j < count && extra_count < 5; j++)
      array->extra_strides[extra_count++] = strides[j];
    array->extra_strides_count = (unsigned char)extra_count;
    array->ptr_array_pointee_bytes = total_size;
    type = array;
  }
  return type;
}

static psx_type_t *legacy_decl_shape_reconstruct_pointer_to_array_base(
    const legacy_decl_shape_t *mem) {
  if (!mem || mem->ptr_array_pointee_bytes <= 0 ||
      mem->pointer_qual_levels <= 0 || mem->type_size > 8 ||
      mem->compound_literal_array_size > 0) {
    return NULL;
  }
  int scalar_size = mem->base_deref_size > 0 ? mem->base_deref_size
                                             : mem->deref_size;
  if (scalar_size <= 0) scalar_size = mem->pointee_is_bool ? 1 : 4;
  psx_type_t *scalar = legacy_decl_shape_reconstruct_scalar_pointee(mem, scalar_size);
  int points_to_pointer_element =
      mem->pointee_is_scalar_ptr &&
      mem->type_size == 8 &&
      mem->base_deref_size > 0 &&
      mem->base_deref_size < 8 &&
      mem->ptr_array_pointee_bytes > 8 &&
      (mem->ptr_array_pointee_bytes % 8) == 0;
  if (points_to_pointer_element) {
    psx_type_t *elem_ptr = psx_type_new_pointer(scalar, scalar_size);
    elem_ptr->base_deref_size = scalar_size;
    elem_ptr->pointer_qual_levels = 1;
    psx_type_t *row = legacy_decl_shape_reconstruct_pointer_element_array_shape(
        mem, elem_ptr, 8, scalar_size);
    if (!row) return NULL;
    if (mem->pointer_qual_levels <= 1) return row;
    psx_type_t *row_ptr = psx_type_new_pointer(row, mem->ptr_array_pointee_bytes);
    row_ptr->base_deref_size = scalar_size;
    row_ptr->pointer_qual_levels = 1;
    row_ptr->outer_stride = mem->ptr_array_pointee_bytes;
    return row_ptr;
  }
  int row_len = scalar_size > 0 ? mem->ptr_array_pointee_bytes / scalar_size : 0;
  if (row_len <= 0) row_len = 1;
  psx_type_t *row = psx_type_new_array(scalar, row_len,
                                       mem->ptr_array_pointee_bytes,
                                       scalar_size, 0);
  row->base_deref_size = scalar_size;
  if (mem->next_deref_size > 0) {
    row->deref_size = mem->next_deref_size;
    row->outer_stride = mem->next_deref_size;
  }
  if (mem->extra_strides_count > 0) {
    row->mid_stride = mem->extra_strides[0];
    int shifted = mem->extra_strides_count - 1;
    row->extra_strides_count = (unsigned char)shifted;
    for (int i = 0; i < shifted && i < 5; i++)
      row->extra_strides[i] = mem->extra_strides[i + 1];
  }
  row->ptr_array_pointee_bytes = mem->ptr_array_pointee_bytes;
  if (mem->pointer_qual_levels <= 1) return row;

  psx_type_t *row_ptr = psx_type_new_pointer(row, mem->ptr_array_pointee_bytes);
  row_ptr->base_deref_size = scalar_size;
  row_ptr->pointer_qual_levels = 1;
  row_ptr->outer_stride = mem->ptr_array_pointee_bytes;
  return row_ptr;
}

static psx_type_t *legacy_decl_shape_reconstruct_array_shape_base(
    const legacy_decl_shape_t *mem) {
  if (!mem || mem->deref_size <= 0 || mem->inner_deref_size <= 0 ||
      mem->deref_size <= mem->inner_deref_size ||
      mem->ptr_array_pointee_bytes > 0) {
    return NULL;
  }

  int strides[8] = {0};
  int count = 0;
  strides[count++] = mem->deref_size;
  strides[count++] = mem->inner_deref_size;
  if (mem->next_deref_size > 0 && count < 8) strides[count++] = mem->next_deref_size;
  for (int i = 0; i < mem->extra_strides_count && i < 5 && count < 8; i++) {
    if (mem->extra_strides[i] > 0) strides[count++] = mem->extra_strides[i];
  }
  if (count < 2) return NULL;

  int scalar_size = strides[count - 1];
  if (scalar_size <= 0) {
    scalar_size = mem->base_deref_size > 0 ? mem->base_deref_size : 4;
  }
  psx_type_t *base = NULL;
  if (psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) {
    base = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                            mem->tag_scope_depth_p1, scalar_size);
  } else if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    base = psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind, scalar_size);
  } else {
    base = psx_type_new_integer(legacy_decl_shape_array_leaf_is_bool(mem) ? TK_BOOL : TK_EOF,
                                scalar_size,
                                legacy_decl_shape_array_leaf_is_unsigned(mem));
  }

  for (int i = count - 2; i >= 0; i--) {
    int total_size = strides[i];
    int elem_size = strides[i + 1];
    if (total_size <= 0 || elem_size <= 0) return base;
    int array_len = total_size / elem_size;
    if (array_len <= 0) array_len = 1;
    psx_type_t *array = psx_type_new_array(base, array_len, total_size,
                                           elem_size, 0);
    array->base_deref_size = scalar_size;
    array->outer_stride = elem_size;
    array->mid_stride = (i + 2 < count) ? strides[i + 2] : 0;
    int extra_count = 0;
    for (int j = i + 3; j < count && extra_count < 5; j++) {
      array->extra_strides[extra_count++] = strides[j];
    }
    array->extra_strides_count = (unsigned char)extra_count;
    base = array;
  }
  return base;
}

static psx_type_t *type_with_funcptr_sig(psx_type_t *type,
                                         psx_decl_funcptr_sig_t sig) {
  if (!type || !psx_decl_funcptr_sig_has_payload(sig) ||
      psx_decl_funcptr_sig_has_payload(type->funcptr_sig)) {
    return type;
  }
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  return copy;
}

static int funcptr_sig_equal(psx_decl_funcptr_sig_t a, psx_decl_funcptr_sig_t b) {
  return psx_funcptr_type_shape_matches(a.function, b.function);
}

static psx_type_t *type_with_funcptr_sig_merged(psx_type_t *type,
                                                psx_decl_funcptr_sig_t sig) {
  if (!type || !psx_decl_funcptr_sig_has_payload(sig)) return type;
  psx_decl_funcptr_sig_t merged =
      funcptr_sig_merge_missing(type->funcptr_sig, &sig, 1);
  if (funcptr_sig_equal(type->funcptr_sig, merged)) return type;
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->funcptr_sig = psx_decl_funcptr_sig_clone(merged);
  return copy;
}

static psx_type_t *type_with_self_qualifiers(psx_type_t *type,
                                             int is_const_qualified,
                                             int is_volatile_qualified) {
  if (!type) return NULL;
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->funcptr_sig = psx_decl_funcptr_sig_clone(type->funcptr_sig);
  if (type_is_pointer_view_type(copy)) {
    if (is_const_qualified) copy->pointer_const_qual_mask |= 1u;
    else copy->pointer_const_qual_mask &= ~1u;
    if (is_volatile_qualified) copy->pointer_volatile_qual_mask |= 1u;
    else copy->pointer_volatile_qual_mask &= ~1u;
  } else {
    copy->is_const_qualified = is_const_qualified ? 1 : 0;
    copy->is_volatile_qualified = is_volatile_qualified ? 1 : 0;
  }
  return copy;
}

static psx_type_t *legacy_decl_shape_reconstruct_type(const legacy_decl_shape_t *mem,
                                               int force_array,
                                               int force_vla) {
  if (!mem) return NULL;

  psx_type_t *type = NULL;
  int looks_like_array_decay =
      !mem->is_tag_pointer && !mem->is_scalar_ptr_member &&
      mem->type_size > mem->deref_size && mem->deref_size > 0 &&
      (mem->type_size % mem->deref_size) == 0 &&
      ((!mem->is_pointer && mem->tag_kind == TK_EOF) ||
       (mem->is_pointer && mem->pointer_qual_levels == 0 &&
        (mem->is_array_member || mem->type_size > 8)));

  if (force_array || looks_like_array_decay) {
    int elem_size = mem->deref_size > 0 ? mem->deref_size : mem->base_deref_size;
    if (elem_size <= 0 && mem->is_tag_pointer) elem_size = 8;
    if (elem_size <= 0) elem_size = legacy_decl_shape_tag_pointee_size(mem);
    if (elem_size <= 0) elem_size = mem->type_size;
    int array_len = (elem_size > 0 && mem->type_size > 0 &&
                     (mem->type_size % elem_size) == 0)
                        ? mem->type_size / elem_size
                        : 0;
    type = legacy_decl_shape_reconstruct_array_shape(mem, elem_size, force_vla);
    int has_canonical_array_shape = type != NULL;
    if (!type) {
      psx_type_t *base = legacy_decl_shape_reconstruct_array_base(mem);
      type = psx_type_new_array(base, array_len, mem->type_size, elem_size, force_vla);
    }
    int canonical_deref_size = type ? type->deref_size : 0;
    int canonical_outer_stride = type ? type->outer_stride : 0;
    int canonical_mid_stride = type ? type->mid_stride : 0;
    int canonical_extra_strides[5] = {0};
    int canonical_extra_strides_count = type ? type->extra_strides_count : 0;
    if (type) {
      for (int i = 0; i < 5; i++) canonical_extra_strides[i] = type->extra_strides[i];
    }
    psx_type_copy_pointer_metadata(type, (psx_type_t[]){
      {
        .deref_size = mem->deref_size,
        .base_deref_size = mem->base_deref_size,
        .pointer_qual_levels = mem->pointer_qual_levels,
        .pointee_fp_kind = (tk_float_kind_t)mem->pointee_fp_kind,
        .vla_row_stride_frame_off = mem->vla_row_stride_frame_off,
        .vla_strides_remaining = mem->vla_strides_remaining,
        .ptr_array_pointee_bytes = mem->ptr_array_pointee_bytes,
        .outer_stride = mem->inner_deref_size,
        .mid_stride = mem->next_deref_size,
        .extra_strides_count = mem->extra_strides_count,
        .extra_strides = {mem->extra_strides[0], mem->extra_strides[1], mem->extra_strides[2],
                          mem->extra_strides[3], mem->extra_strides[4]},
      }
    });
    if (has_canonical_array_shape && type) {
      type->deref_size = canonical_deref_size;
      type->outer_stride = canonical_outer_stride;
      type->mid_stride = canonical_mid_stride;
      type->extra_strides_count = (unsigned char)canonical_extra_strides_count;
      for (int i = 0; i < 5; i++) type->extra_strides[i] = canonical_extra_strides[i];
    }
  } else if (mem->is_pointer || mem->is_tag_pointer ||
             legacy_decl_shape_has_pointee_payload(mem)) {
    psx_type_t *base = legacy_decl_shape_reconstruct_pointer_array_base(mem);
    int base_is_array_shape = base != NULL;
    if (!base) {
      base = legacy_decl_shape_reconstruct_pointer_to_array_base(mem);
      base_is_array_shape = base && base->kind == PSX_TYPE_ARRAY;
    }
    if (!base) {
      base = legacy_decl_shape_reconstruct_array_shape_base(mem);
      base_is_array_shape = base != NULL;
    }
    if (!base) base = legacy_decl_shape_reconstruct_pointee_base(mem);
    int has_array_stride_shape =
        mem->inner_deref_size > 0 || mem->next_deref_size > 0 ||
        mem->extra_strides_count > 0;
    if (!has_array_stride_shape) {
      base = legacy_decl_shape_reconstruct_inner_pointer_levels(base, mem);
    }
    int outer_deref = base_is_array_shape ? psx_type_sizeof(base)
                      : ((mem->pointer_qual_levels >= 2 && !has_array_stride_shape)
                             ? 8
                             : mem->deref_size);
    if (outer_deref <= 0 && has_array_stride_shape && mem->inner_deref_size > 0)
      outer_deref = mem->inner_deref_size;
    type = psx_type_new_pointer(base, outer_deref);
    psx_type_copy_pointer_metadata(type, (psx_type_t[]){
      {
        .deref_size = outer_deref,
        .base_deref_size = mem->base_deref_size,
        .pointer_qual_levels = mem->pointer_qual_levels,
        .pointer_const_qual_mask = mem->pointer_const_qual_mask,
        .pointer_volatile_qual_mask = mem->pointer_volatile_qual_mask,
        .pointee_fp_kind = (tk_float_kind_t)mem->pointee_fp_kind,
        .vla_row_stride_frame_off = mem->vla_row_stride_frame_off,
        .vla_strides_remaining = mem->vla_strides_remaining,
        .ptr_array_pointee_bytes = mem->ptr_array_pointee_bytes,
        .outer_stride = mem->inner_deref_size,
        .mid_stride = mem->next_deref_size,
        .extra_strides_count = mem->extra_strides_count,
        .extra_strides = {mem->extra_strides[0], mem->extra_strides[1], mem->extra_strides[2],
                          mem->extra_strides[3], mem->extra_strides[4]},
      }
    });
  } else if (!mem->is_tag_pointer && psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) {
    type = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                            mem->tag_scope_depth_p1, mem->type_size);
  } else if (mem->is_complex) {
    type = psx_type_new(PSX_TYPE_COMPLEX);
    type->size = mem->type_size;
    type->align = mem->type_size >= 8 ? 8 : (mem->type_size >= 4 ? 4 : 1);
    type->fp_kind = mem->fp_kind;
  } else if (mem->fp_kind != TK_FLOAT_KIND_NONE) {
    type = psx_type_new_float(mem->fp_kind, mem->type_size);
  } else {
    type = psx_type_new_integer(mem->is_bool ? TK_BOOL : TK_EOF, mem->type_size,
                                mem->is_unsigned);
  }

  if (type) {
    if ((type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) && type->base) {
      if (mem->is_const_qualified) type->base->is_const_qualified = 1;
      if (mem->is_volatile_qualified) type->base->is_volatile_qualified = 1;
    }
    if (psx_type_is_tag_aggregate(type)) {
      type->tag_kind = mem->tag_kind;
      type->tag_name = mem->tag_name;
      type->tag_len = mem->tag_len;
      type->tag_scope_depth_p1 = mem->tag_scope_depth_p1;
    }
    type->is_const_qualified = mem->is_const_qualified;
    type->is_volatile_qualified = mem->is_volatile_qualified;
    type->is_atomic = mem->is_atomic;
    type->is_long_long = mem->is_long_long;
    type->is_plain_char = mem->is_plain_char;
    type->is_long_double = mem->is_long_double;
    legacy_decl_shape_copy_funcptr_metadata(type, mem);
  }
  return type;
}

static psx_type_t *type_from_legacy_decl_shape(const legacy_decl_shape_t *mem,
                                             int force_array, int force_vla) {
  return legacy_decl_shape_reconstruct_type(mem, force_array, force_vla);
}

static psx_type_t *type_clone_arena(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = arena_alloc(sizeof(psx_type_t));
  *dst = *src;
  dst->base = type_clone_arena(src->base);
  dst->funcptr_sig = psx_decl_funcptr_sig_clone(src->funcptr_sig);
  return dst;
}

static psx_type_t *lvar_decl_type_consistent(lvar_t *var);
static psx_type_t *gvar_decl_type_consistent(global_var_t *gv);

static psx_type_t *lvar_decl_type_view(const lvar_t *var) {
  return var ? lvar_decl_type_consistent((lvar_t *)var) : NULL;
}

static psx_type_t *gvar_decl_type_view(const global_var_t *gv) {
  return gv ? gvar_decl_type_consistent((global_var_t *)gv) : NULL;
}

static token_kind_t type_tag_aggregate_kind(const psx_type_t *type) {
  if (!type) return TK_EOF;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (!type) return TK_EOF;
  if (type->kind == PSX_TYPE_STRUCT) return TK_STRUCT;
  if (type->kind == PSX_TYPE_UNION) return TK_UNION;
  return TK_EOF;
}

static psx_type_t *lvar_decl_type_consistent(lvar_t *var) {
  return psx_lvar_materialize_decl_type(var);
}

static psx_type_t *gvar_decl_type_consistent(global_var_t *gv) {
  return psx_gvar_materialize_decl_type(gv);
}

static int lvar_is_pointer_like_from_fields(const lvar_t *var) {
  if (!var) return 0;
  if (psx_ctx_is_tag_aggregate_kind(var->tag_kind) && !var->is_tag_pointer &&
      !var->is_array && !var->is_vla && var->pointer_qual_levels <= 0 &&
      var->outer_stride <= 0 && var->ptr_array_pointee_bytes <= 0) {
    return 0;
  }
  int is_storage_tag_pointer =
      var->is_tag_pointer && (var->is_array || var->is_vla || var->size == 8);
  return var->is_array || var->is_vla || is_storage_tag_pointer ||
         var->pointer_qual_levels > 0 ||
         (var->size > var->elem_size) ||
         (var->outer_stride > 0 && var->size == 8 && !var->is_array && !var->is_vla) ||
         var->pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         var->pointee_is_void;
}

int psx_lvar_value_is_pointer_like(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  if (type) return psx_type_is_pointer(type);
  return lvar_is_pointer_like_from_fields(var);
}

int psx_lvar_is_struct_aggregate(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  if (type) return type_tag_aggregate_kind(type) == TK_STRUCT;
  return var && var->tag_kind == TK_STRUCT && !var->is_tag_pointer;
}

int psx_lvar_is_union_aggregate(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  if (type) return type_tag_aggregate_kind(type) == TK_UNION;
  return var && var->tag_kind == TK_UNION && !var->is_tag_pointer;
}

int psx_lvar_is_tag_aggregate(const lvar_t *var) {
  return psx_lvar_is_struct_aggregate(var) || psx_lvar_is_union_aggregate(var);
}

static int lvar_pointee_is_bool(const lvar_t *var) {
  psx_type_t *type = (var && var->decl_type) ? lvar_decl_type_view(var) : NULL;
  if (type) {
    const psx_type_t *pointee = type_pointee_value_type(type);
    return pointee && pointee->kind == PSX_TYPE_BOOL ? 1 : 0;
  }
  return var && (var->pointee_is_bool || (var->is_array && var->is_bool)) ? 1 : 0;
}

static int lvar_pointee_is_unsigned(const lvar_t *var) {
  psx_type_t *type = (var && var->decl_type) ? lvar_decl_type_view(var) : NULL;
  if (type) {
    const psx_type_t *pointee = type_pointee_value_type(type);
    return pointee && psx_type_is_unsigned(pointee) ? 1 : 0;
  }
  return var && (var->pointee_is_unsigned || (var->is_array && var->is_unsigned)) ? 1 : 0;
}

static const psx_type_t *type_skip_array_views_const(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static int lvar_self_is_const_qualified(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  if (type) {
    const psx_type_t *value_type = type_skip_array_views_const(type);
    if (type_is_pointer_view_type(value_type))
      return (psx_type_pointer_view_structural_qual_mask(value_type, 0) & 1u)
                 ? 1
                 : 0;
    return value_type && value_type->is_const_qualified ? 1 : 0;
  }
  return var && var->is_const_qualified ? 1 : 0;
}

static int lvar_self_is_volatile_qualified(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  if (type) {
    const psx_type_t *value_type = type_skip_array_views_const(type);
    if (type_is_pointer_view_type(value_type))
      return (psx_type_pointer_view_structural_qual_mask(value_type, 1) & 1u)
                 ? 1
                 : 0;
    return value_type && value_type->is_volatile_qualified ? 1 : 0;
  }
  return var && var->is_volatile_qualified ? 1 : 0;
}

static void init_lvar_decl_shape_from_legacy_fields(legacy_decl_shape_t *mem,
                                             const lvar_t *var) {
  *mem = (legacy_decl_shape_t){0};
  if (!var) return;
  mem->fp_kind = (tk_float_kind_t)var->fp_kind;
  mem->type_size = (short)var->size;
  mem->deref_size = (short)var->elem_size;
  mem->base_deref_size = var->base_deref_size;
  mem->tag_kind = var->tag_kind;
  mem->tag_name = var->tag_name;
  mem->tag_len = var->tag_len;
  mem->tag_scope_depth_p1 = var->tag_scope_depth_p1;
  mem->is_tag_pointer =
      (var->is_tag_pointer && (var->is_array || var->is_vla || var->size == 8)) ? 1 : 0;
  if (var->is_tag_pointer && !lvar_is_pointer_like_from_fields(var)) {
    mem->tag_kind = TK_EOF;
    mem->tag_name = NULL;
    mem->tag_len = 0;
    mem->tag_scope_depth_p1 = 0;
  }
  mem->is_pointer = lvar_is_pointer_like_from_fields(var) ? 1 : 0;
  mem->is_unsigned = var->is_unsigned ? 1 : 0;
  mem->is_const_qualified = var->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = var->is_volatile_qualified ? 1 : 0;
  mem->is_pointer_const_qualified = var->is_pointer_const_qualified ? 1 : 0;
  mem->is_pointer_volatile_qualified = var->is_pointer_volatile_qualified ? 1 : 0;
  mem->is_complex = var->is_complex ? 1 : 0;
  mem->is_atomic = var->is_atomic ? 1 : 0;
  mem->pointee_is_void = var->pointee_is_void ? 1 : 0;
  mem->is_bool = var->is_bool ? 1 : 0;
  mem->is_long_long = var->is_long_long ? 1 : 0;
  mem->is_plain_char = var->is_plain_char ? 1 : 0;
  mem->is_long_double = var->is_long_double ? 1 : 0;
  mem->pointee_is_bool = lvar_pointee_is_bool(var);
  mem->pointee_is_unsigned = lvar_pointee_is_unsigned(var);
  mem->pointee_fp_kind = (unsigned int)var->pointee_fp_kind;
  if (var->is_array && var->fp_kind != TK_FLOAT_KIND_NONE)
    mem->pointee_fp_kind = (unsigned int)var->fp_kind;
  psx_decl_funcptr_sig_t funcptr_sig =
      psx_decl_funcptr_sig_clone(var->funcptr_sig);
  legacy_decl_shape_store_funcptr_signature(mem, &funcptr_sig);
  mem->pointer_const_qual_mask = var->pointer_const_qual_mask;
  mem->pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  mem->pointer_qual_levels = var->pointer_qual_levels;
  mem->inner_deref_size = (short)var->outer_stride;
  mem->next_deref_size = (short)var->mid_stride;
  mem->extra_strides_count = var->extra_strides_count;
  for (int i = 0; i < var->extra_strides_count && i < 5; i++) {
    mem->extra_strides[i] = var->extra_strides[i];
  }
  mem->vla_row_stride_frame_off = var->vla_row_stride_frame_off;
  mem->vla_strides_remaining = var->vla_strides_remaining;
  mem->ptr_array_pointee_bytes = var->ptr_array_pointee_bytes;
  if (((var->is_array && var->pointer_qual_levels > 0) ||
       (var->ptr_array_pointee_bytes > 0 && var->pointer_qual_levels > 0)) &&
      var->base_deref_size > 0 && var->elem_size > var->base_deref_size) {
    mem->pointee_is_scalar_ptr = 1;
  }
}

static void init_lvar_decl_shape(legacy_decl_shape_t *mem, lvar_t *var) {
  init_lvar_decl_shape_from_legacy_fields(mem, var);
}

static void sync_materialized_lvar_runtime_shape(lvar_t *var, psx_type_t *type) {
  if (!var || !type) return;
  if (var->is_vla) {
    type->is_vla = 1;
    type->vla_row_stride_frame_off = var->vla_row_stride_frame_off;
    type->vla_strides_remaining = var->vla_strides_remaining;
  }
  int sync_runtime_shape =
      type->kind == PSX_TYPE_POINTER ||
      (type->kind == PSX_TYPE_ARRAY && var->is_vla);
  if (sync_runtime_shape && var->outer_stride > 0) {
    type->outer_stride = var->outer_stride;
  }
  if (sync_runtime_shape && var->mid_stride > 0) {
    type->mid_stride = var->mid_stride;
  }
  if (sync_runtime_shape && var->extra_strides_count > 0) {
    type->extra_strides_count = var->extra_strides_count;
    for (int i = 0; i < var->extra_strides_count && i < 5; i++) {
      type->extra_strides[i] = var->extra_strides ? var->extra_strides[i] : 0;
    }
  }
}

static psx_type_t *canonical_lvar_vla_type(lvar_t *var,
                                           psx_type_t *reconstructed) {
  if (!var || !var->is_vla || var->pointer_qual_levels > 0 || !reconstructed)
    return reconstructed;

  psx_type_t *leaf = reconstructed;
  while (leaf->kind == PSX_TYPE_ARRAY && leaf->base) leaf = leaf->base;

  int leaf_size = psx_type_sizeof(leaf);
  if (leaf_size <= 0) leaf_size = var->elem_size;
  if (leaf_size <= 0) return reconstructed;

  psx_type_t *element = leaf;
  if (var->vla_row_stride_frame_off == 0 &&
      var->outer_stride > leaf_size &&
      (var->outer_stride % leaf_size) == 0) {
    int row_size = var->outer_stride;
    element = psx_type_new_array(leaf, row_size / leaf_size,
                                 row_size, leaf_size, 0);
    element->base_deref_size = leaf_size;
  }

  int element_size = psx_type_sizeof(element);
  if (element_size <= 0) element_size = leaf_size;
  psx_type_t *vla = psx_type_new_array(element, 0, 0, element_size, 1);
  vla->base_deref_size = leaf_size;
  vla->pointee_fp_kind = reconstructed->pointee_fp_kind;
  vla->funcptr_sig = psx_decl_funcptr_sig_clone(reconstructed->funcptr_sig);
  psx_type_copy_common_qualifiers(vla, reconstructed);
  return vla;
}

static void sync_materialized_gvar_runtime_shape(global_var_t *gv, psx_type_t *type) {
  if (!gv || !type) return;
  int sync_runtime_shape = type->kind == PSX_TYPE_POINTER ||
                           (type->kind == PSX_TYPE_ARRAY && type->is_vla);
  if (sync_runtime_shape && gv->outer_stride > 0) {
    type->outer_stride = gv->outer_stride;
  }
  if (sync_runtime_shape && gv->mid_stride > 0) {
    type->mid_stride = gv->mid_stride;
  }
  if (sync_runtime_shape && gv->extra_strides_count > 0) {
    type->extra_strides_count = gv->extra_strides_count;
    for (int i = 0; i < gv->extra_strides_count && i < 5; i++) {
      type->extra_strides[i] = gv->extra_strides[i];
    }
  }
}

psx_type_t *psx_lvar_get_decl_type(lvar_t *var) {
  return lvar_decl_type_consistent(var);
}

psx_type_t *psx_lvar_materialize_decl_type(lvar_t *var) {
  if (!var) return NULL;
  if (var->decl_type) {
    if (var->ptr_array_pointee_bytes <= 0)
      sync_materialized_lvar_runtime_shape(var, var->decl_type);
    if (var->type_sig) var->decl_type->type_sig = var->type_sig;
    return var->decl_type;
  }
  legacy_decl_shape_t mem;
  init_lvar_decl_shape(&mem, var);
  if (var->is_byref_param && psx_ctx_is_tag_aggregate_kind(var->tag_kind) &&
      !var->is_tag_pointer && var->elem_size > 0) {
    mem.type_size = (short)var->elem_size;
  }
  int force_array = var->is_array || (var->is_vla && var->pointer_qual_levels == 0);
  var->decl_type =
      type_from_legacy_decl_shape(&mem, force_array, force_array && var->is_vla);
  var->decl_type = canonical_lvar_vla_type(var, var->decl_type);
  psx_type_canonicalize_flat_pointer_to_array(var->decl_type);
  sync_materialized_lvar_runtime_shape(var, var->decl_type);
  if (var->decl_type) var->decl_type->type_sig = var->type_sig;
  return var->decl_type;
}

static int gvar_is_pointer_like_from_fields(const global_var_t *gv) {
  if (!gv) return 0;
  if (psx_ctx_is_tag_aggregate_kind(gv->tag_kind) && !gv->is_tag_pointer &&
      !gv->is_array && gv->pointer_qual_levels <= 0 &&
      gv->outer_stride <= 0 && gv->ptr_array_pointee_bytes <= 0) {
    return 0;
  }
  int is_storage_tag_pointer =
      gv->is_tag_pointer && (gv->is_array || gv->type_size == 8);
  return gv->is_array || is_storage_tag_pointer || gv->pointer_qual_levels > 0 ||
         gv->outer_stride > 0 ||
         gv->ptr_array_pointee_bytes > 0 ||
         gv->pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         (gv->type_size == 8 && gv->deref_size > 0 && gv->deref_size < gv->type_size);
}

int psx_gvar_storage_size(const global_var_t *gv, int fallback_size) {
  int decl_size = psx_gvar_decl_sizeof(gv, 0);
  int storage_size = gv && gv->type_size > 0 ? gv->type_size : 0;
  if (storage_size > decl_size) return storage_size;
  if (decl_size > 0) return decl_size;
  return storage_size > 0 ? storage_size : fallback_size;
}

int psx_gvar_decl_sizeof(const global_var_t *gv, int fallback_size) {
  psx_type_t *type = gvar_decl_type_view(gv);
  int size = psx_type_sizeof(type);
  if (size > 0) return size;
  return gv && gv->type_size > 0 ? gv->type_size : fallback_size;
}

int psx_gvar_is_array(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type) return type->kind == PSX_TYPE_ARRAY ? 1 : 0;
  return gv && gv->is_array ? 1 : 0;
}

int psx_gvar_is_struct_aggregate(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type) return type_tag_aggregate_kind(type) == TK_STRUCT;
  return gv && gv->tag_kind == TK_STRUCT && !gv->is_tag_pointer;
}

int psx_gvar_is_union_aggregate(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type) return type_tag_aggregate_kind(type) == TK_UNION;
  return gv && gv->tag_kind == TK_UNION && !gv->is_tag_pointer;
}

int psx_gvar_is_tag_aggregate(const global_var_t *gv) {
  return psx_gvar_is_struct_aggregate(gv) || psx_gvar_is_union_aggregate(gv);
}

static const psx_type_t *type_skip_arrays_const(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

int psx_gvar_is_bool_scalar(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type) return type->kind == PSX_TYPE_BOOL ? 1 : 0;
  return gv && gv->is_bool ? 1 : 0;
}

int psx_gvar_array_element_is_bool(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type) {
    if (type->kind != PSX_TYPE_ARRAY) return 0;
    const psx_type_t *leaf = type_skip_arrays_const(type);
    return leaf && leaf->kind == PSX_TYPE_BOOL ? 1 : 0;
  }
  return gv && gv->elem_is_bool ? 1 : 0;
}

psx_gvar_initializer_class_t
psx_gvar_initializer_class(const global_var_t *gv, int include_empty_aggregate) {
  psx_gvar_view_t view = psx_gvar_view(gv);
  int is_tag_aggregate = psx_gvar_is_tag_aggregate(gv);
  psx_gvar_initializer_class_t cls = {
      .kind = PSX_GVAR_INIT_KIND_INTEGER,
      .is_tag_aggregate = is_tag_aggregate,
      .has_aggregate_initializer = is_tag_aggregate && view.init_count > 0,
      .has_explicit_initializer = view.has_init,
      .has_payload = 0,
  };
  if (is_tag_aggregate) {
    cls.has_payload = cls.has_aggregate_initializer;
    if (include_empty_aggregate || cls.has_aggregate_initializer) {
      cls.kind = PSX_GVAR_INIT_KIND_AGGREGATE;
    }
    return cls;
  }
  if (view.init_symbol) {
    cls.kind = PSX_GVAR_INIT_KIND_SYMBOL;
    cls.has_payload = 1;
    return cls;
  }
  if (view.init_count > 0) {
    cls.kind = PSX_GVAR_INIT_KIND_SLOTS;
    cls.has_payload = 1;
    return cls;
  }
  if (view.fp_kind != TK_FLOAT_KIND_NONE) {
    cls.kind = PSX_GVAR_INIT_KIND_FLOAT;
    cls.has_payload = 1;
    return cls;
  }
  cls.has_payload = view.has_init;
  return cls;
}

int psx_gvar_has_aggregate_initializer(const global_var_t *gv) {
  return psx_gvar_initializer_class(gv, 0).has_aggregate_initializer;
}

int psx_gvar_has_explicit_initializer(const global_var_t *gv) {
  return psx_gvar_initializer_class(gv, 0).has_explicit_initializer;
}

static psx_gvar_init_slots_layout_t gvar_init_slots_layout(const global_var_t *gv,
                                                           int fallback_size) {
  psx_gvar_view_t view = psx_gvar_view(gv);
  psx_gvar_init_slots_layout_t layout = {
      .elem_size = psx_gvar_initializer_element_size(gv, fallback_size),
      .elem_count = psx_gvar_initializer_element_count(gv, fallback_size),
      .init_count = view.init_count,
      .is_fp_array = view.has_init_fvalues &&
                     (view.fp_kind == TK_FLOAT_KIND_FLOAT ||
                      view.fp_kind >= TK_FLOAT_KIND_DOUBLE),
      .fp_kind = view.fp_kind,
  };
  return layout;
}

static psx_gvar_symbol_ref_t gvar_make_symbol_ref(char *symbol, int symbol_len,
                                                  long long addend) {
  psx_gvar_symbol_ref_t ref = {
      .kind = PSX_GVAR_SYMBOL_REF_NONE,
      .symbol = NULL,
      .symbol_len = 0,
      .addend = 0,
  };
  if (!symbol) return ref;
  ref.symbol = symbol;
  ref.addend = addend;
  if (symbol_len < 0) {
    ref.kind = PSX_GVAR_SYMBOL_REF_STRING_LITERAL;
    return ref;
  }
  if (symbol_len > 0) {
    ref.kind = PSX_GVAR_SYMBOL_REF_NAMED;
    ref.symbol_len = symbol_len;
  }
  return ref;
}

static psx_gvar_symbol_ref_t gvar_initializer_symbol_ref(const global_var_t *gv) {
  if (!gv) return gvar_make_symbol_ref(NULL, 0, 0);
  psx_gvar_view_t view = psx_gvar_view(gv);
  return gvar_make_symbol_ref(view.init_symbol, view.init_symbol_len,
                              view.init_symbol_offset);
}

static psx_gvar_symbol_ref_t gvar_init_slot_symbol_ref(const psx_gvar_init_slot_t *slot) {
  if (!slot) return gvar_make_symbol_ref(NULL, 0, 0);
  return gvar_make_symbol_ref(slot->symbol, slot->symbol_len, slot->value);
}

static psx_gvar_init_slot_value_t gvar_init_slot_value(
    const global_var_t *gv, int idx, const psx_gvar_init_slots_layout_t *layout) {
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, idx);
  psx_gvar_init_slot_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_init_slot_symbol_ref(&slot),
      .value = slot.value,
      .fvalue = slot.fvalue,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = layout ? layout->elem_size : 0,
  };
  if (value.symbol_ref.kind != PSX_GVAR_SYMBOL_REF_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_SYMBOL;
    return value;
  }
  if (layout && layout->is_fp_array) {
    value.kind = PSX_GVAR_INIT_VALUE_FLOAT;
    value.fp_kind = layout->fp_kind;
  }
  return value;
}

int psx_gvar_walk_init_slot_values(const global_var_t *gv,
                                   const psx_gvar_init_slots_layout_t *layout,
                                   int value_count,
                                   psx_gvar_init_slot_value_fn callback,
                                   void *user) {
  if (!layout || !callback) return 0;
  int count = value_count < 0 ? layout->elem_count : value_count;
  if (count > layout->elem_count) count = layout->elem_count;
  if (count < 0) count = 0;
  for (int i = 0; i < count; i++) {
    psx_gvar_init_slot_value_t value = gvar_init_slot_value(gv, i, layout);
    if (!callback(user, i, value, layout)) return 0;
  }
  return 1;
}

int psx_gvar_fp_bit_pattern(tk_float_kind_t fp_kind, double value,
                            psx_gvar_fp_bits_t *out) {
  if (!out) return 0;
  out->bits = 0;
  out->size = 0;
  if (fp_kind == TK_FLOAT_KIND_FLOAT) {
    float f = (float)value;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    out->bits = bits;
    out->size = 4;
    return 1;
  }
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    out->bits = (unsigned long long)bits;
    out->size = 8;
    return 1;
  }
  return 0;
}

int psx_gvar_symbol_ref_named(psx_gvar_symbol_ref_t ref,
                              char **out_name, int *out_len) {
  if (out_name) *out_name = NULL;
  if (out_len) *out_len = 0;
  if (ref.kind != PSX_GVAR_SYMBOL_REF_NAMED) return 0;
  if (out_name) *out_name = ref.symbol;
  if (out_len) *out_len = ref.symbol_len;
  return 1;
}

int psx_gvar_symbol_ref_named_function(psx_gvar_symbol_ref_t ref,
                                       char **out_name, int *out_len) {
  char *name = NULL;
  int len = 0;
  if (!psx_gvar_symbol_ref_named(ref, &name, &len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (!psx_ctx_has_function_name(name, len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (out_name) *out_name = name;
  if (out_len) *out_len = len;
  return 1;
}

int psx_gvar_init_value_named_function(psx_gvar_init_value_t value,
                                        char **out_name, int *out_len) {
  if (out_name) *out_name = NULL;
  if (out_len) *out_len = 0;
  if (value.kind != PSX_GVAR_INIT_VALUE_SYMBOL) return 0;
  return psx_gvar_symbol_ref_named_function(value.symbol_ref, out_name, out_len);
}

psx_gvar_init_member_value_t
psx_gvar_init_member_value(const global_var_t *gv, int idx,
                           const tag_member_info_t *member) {
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, idx);
  tk_float_kind_t member_fp_kind = psx_tag_member_decl_fp_kind(member);
  psx_gvar_init_member_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_init_slot_symbol_ref(&slot),
      .value = slot.value,
      .fvalue = slot.fvalue,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = psx_tag_member_decl_value_size(member),
  };
  if (psx_tag_member_decl_is_bool(member)) value.value = value.value != 0;
  if (value.symbol_ref.kind != PSX_GVAR_SYMBOL_REF_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_SYMBOL;
    return value;
  }
  if (slot.fp_sentinel_kind != TK_FLOAT_KIND_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_FLOAT;
    value.fp_kind = slot.fp_sentinel_kind;
    value.size = value.fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 4;
    return value;
  }
  if (member_fp_kind != TK_FLOAT_KIND_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_FLOAT;
    value.fp_kind = member_fp_kind;
  }
  return value;
}

psx_gvar_init_scalar_value_t
psx_gvar_init_scalar_value(const global_var_t *gv, int fallback_size) {
  psx_gvar_view_t view = psx_gvar_view(gv);
  psx_gvar_init_scalar_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_initializer_symbol_ref(gv),
      .value = view.has_init ? view.init_val : 0,
      .fvalue = view.has_init ? view.fval : 0.0,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = psx_gvar_storage_size(gv, fallback_size),
  };
  if (value.symbol_ref.kind != PSX_GVAR_SYMBOL_REF_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_SYMBOL;
    return value;
  }
  if (view.fp_kind != TK_FLOAT_KIND_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_FLOAT;
    value.fp_kind = view.fp_kind;
  }
  return value;
}

int psx_gvar_visit_initializer_classified(
    const global_var_t *gv, const psx_gvar_initializer_class_t *init_class,
    int fallback_size, const psx_gvar_initializer_visit_ops_t *ops, void *user) {
  if (!init_class || !ops) return 0;
  if (init_class->kind == PSX_GVAR_INIT_KIND_AGGREGATE) {
    return ops->aggregate ? ops->aggregate(user, init_class) : 0;
  }
  if (init_class->kind == PSX_GVAR_INIT_KIND_SLOTS) {
    psx_gvar_init_slots_layout_t layout =
        gvar_init_slots_layout(gv, fallback_size);
    return ops->slots ? ops->slots(user, &layout, init_class) : 0;
  }
  psx_gvar_init_scalar_value_t value =
      psx_gvar_init_scalar_value(gv, fallback_size);
  return ops->scalar ? ops->scalar(user, value, init_class) : 0;
}

int psx_gvar_visit_initializer(const global_var_t *gv, int include_empty_aggregate,
                               int fallback_size,
                               const psx_gvar_initializer_visit_ops_t *ops,
                               void *user) {
  psx_gvar_initializer_class_t init_class =
      psx_gvar_initializer_class(gv, include_empty_aggregate);
  return psx_gvar_visit_initializer_classified(gv, &init_class, fallback_size,
                                               ops, user);
}

int psx_gvar_array_element_size(const global_var_t *gv) {
  if (!psx_gvar_is_array(gv)) return 0;
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type && type->kind == PSX_TYPE_ARRAY) {
    int elem = type->elem_size > 0 ? type->elem_size : psx_type_sizeof(type->base);
    if (elem > 0) return elem;
  }
  if (psx_gvar_is_tag_aggregate(gv)) {
    token_kind_t tag_kind = TK_EOF;
    char *tag_name = NULL;
    int tag_len = 0;
    int tag_scope_depth_p1 = 0;
    gvar_tag_identity(gv, &tag_kind, &tag_name, &tag_len, &tag_scope_depth_p1);
    return tag_aggregate_size_from_ctx(tag_kind, tag_name, tag_len,
                                       tag_scope_depth_p1,
                                       gv->deref_size > 0 ? gv->deref_size : 0);
  }
  return gv->deref_size > 0 ? gv->deref_size : 0;
}

int psx_gvar_array_element_count(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type && type->kind == PSX_TYPE_ARRAY && type->array_len > 0)
    return type->array_len;
  int elem = psx_gvar_array_element_size(gv);
  int size = psx_type_sizeof(type);
  if (size <= 0 && gv) size = gv->type_size;
  if (elem <= 0 || size <= 0) return 0;
  return size / elem;
}

static int type_array_leaf_element_size(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  const psx_type_t *cur = type;
  while (cur && cur->kind == PSX_TYPE_ARRAY && cur->base) {
    if (cur->base->kind != PSX_TYPE_ARRAY) {
      int elem = psx_type_sizeof(cur->base);
      if (elem <= 0) elem = cur->elem_size;
      return elem;
    }
    cur = cur->base;
  }
  return 0;
}

static int gvar_tag_identity_from_type(const global_var_t *gv, token_kind_t *kind,
                                       char **name, int *len,
                                       int *scope_depth_p1) {
  psx_type_t *type = gvar_decl_type_view(gv);
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (!type || !psx_type_is_tag_aggregate(type)) return 0;
  if (kind) *kind = type->tag_kind;
  if (name) *name = type->tag_name;
  if (len) *len = type->tag_len;
  if (scope_depth_p1) *scope_depth_p1 = type->tag_scope_depth_p1;
  return 1;
}

static void gvar_tag_identity(const global_var_t *gv, token_kind_t *kind,
                              char **name, int *len, int *scope_depth_p1) {
  token_kind_t out_kind = TK_EOF;
  char *out_name = NULL;
  int out_len = 0;
  int out_scope_depth_p1 = 0;
  if (!gvar_tag_identity_from_type(gv, &out_kind, &out_name, &out_len,
                                   &out_scope_depth_p1) && gv) {
    out_kind = gv->tag_kind;
    out_name = gv->tag_name;
    out_len = gv->tag_len;
    out_scope_depth_p1 = gv->tag_scope_depth_p1;
  }
  if (kind) *kind = out_kind;
  if (name) *name = out_name;
  if (len) *len = out_len;
  if (scope_depth_p1) *scope_depth_p1 = out_scope_depth_p1;
}

static psx_type_t *type_array_leaf_element_type(psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return NULL;
  psx_type_t *cur = type;
  while (cur && cur->kind == PSX_TYPE_ARRAY && cur->base) {
    if (cur->base->kind != PSX_TYPE_ARRAY) return cur->base;
    cur = cur->base;
  }
  return NULL;
}

static const psx_type_t *type_pointee_value_type(const psx_type_t *type) {
  if (!type_is_pointer_view_type(type) || !type->base) return NULL;
  const psx_type_t *base = type->base;
  while (base && base->kind == PSX_TYPE_ARRAY && base->base) {
    base = base->base;
  }
  return base;
}

static psx_type_t *type_array_element_type_for_size(psx_type_t *type,
                                                    int type_size) {
  if (!type || type->kind != PSX_TYPE_ARRAY || type_size <= 0) return NULL;
  psx_type_t *cur = type;
  while (cur && cur->kind == PSX_TYPE_ARRAY && cur->base) {
    int elem_size = psx_type_sizeof(cur->base);
    if (elem_size <= 0) elem_size = cur->elem_size;
    if (elem_size == type_size) return cur->base;
    cur = cur->base;
  }
  return NULL;
}

typedef struct {
  const global_var_t *gv;
  int index;
  int count;
} gvar_init_cursor_t;

typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  int type_size;
  int elem_size;
  int elem_count;
  int is_array;
  int is_union;
} gvar_aggregate_layout_t;

typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  int ordinal;
  int count;
  psx_tag_flat_cover_state_t cover_state;
} gvar_aggregate_member_iter_t;

static gvar_aggregate_layout_t gvar_aggregate_layout(const global_var_t *gv);
static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(token_kind_t tag_kind,
                                                               char *tag_name,
                                                               int tag_len,
                                                               int tag_scope_depth_p1);
static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      tag_member_info_t *out, int *out_ordinal);
static void gvar_aggregate_member_iter_set_next(gvar_aggregate_member_iter_t *iter,
                                                int next_ordinal);
static int gvar_walk_struct_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                        int tag_scope_depth_p1,
                                        global_var_t *gv, gvar_init_cursor_t *cur,
                                        long long base_offset, int struct_size,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user);
static int gvar_walk_union_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       int tag_scope_depth_p1,
                                       global_var_t *gv, gvar_init_cursor_t *cur,
                                       long long base_offset, int union_size,
                                       const psx_gvar_aggregate_walk_ops_t *ops,
                                       void *user);
static gvar_init_cursor_t gvar_init_cursor(const global_var_t *gv);
static int gvar_init_cursor_has(const gvar_init_cursor_t *cur);
static int gvar_init_cursor_index(const gvar_init_cursor_t *cur);
static int gvar_init_cursor_advance(gvar_init_cursor_t *cur);
static int gvar_init_cursor_consume_plain_zero_padding(gvar_init_cursor_t *cur,
                                                       int start_idx, int target_slots);
static int gvar_init_cursor_consume_tag_zero_padding(token_kind_t tag_kind, char *tag_name,
                                                     int tag_len,
                                                     gvar_init_cursor_t *cur,
                                                     int start_idx);
static int gvar_init_cursor_pack_bitfield_unit(token_kind_t tag_kind, char *tag_name,
                                               int tag_len, int member_index,
                                               gvar_init_cursor_t *cur,
                                               psx_gvar_bitfield_unit_t *out);
static int tag_union_init_member_for_slot_scoped(token_kind_t tag_kind, char *tag_name,
                                                int tag_len, int tag_scope_depth_p1,
                                                const global_var_t *gv, int idx,
                                                tag_member_info_t *out);

static gvar_aggregate_layout_t gvar_aggregate_layout(const global_var_t *gv) {
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int tag_scope_depth_p1 = 0;
  gvar_tag_identity(gv, &tag_kind, &tag_name, &tag_len, &tag_scope_depth_p1);
  int type_size = psx_gvar_decl_sizeof(gv, 0);
  if (type_size <= 0 && gv) type_size = gv->type_size;
  gvar_aggregate_layout_t layout = {
      .tag_kind = tag_kind,
      .tag_name = tag_name,
      .tag_len = tag_len,
      .tag_scope_depth_p1 = tag_scope_depth_p1,
      .type_size = type_size,
      .elem_size = type_size,
      .elem_count = 1,
      .is_array = psx_gvar_is_array(gv),
      .is_union = psx_gvar_is_union_aggregate(gv),
  };
  if (layout.is_array) {
    layout.elem_size = psx_gvar_initializer_element_size(gv, type_size);
    layout.elem_count = psx_gvar_initializer_element_count(gv, type_size);
  }
  return layout;
}

static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(token_kind_t tag_kind,
                                                               char *tag_name,
                                                               int tag_len,
                                                               int tag_scope_depth_p1) {
  gvar_aggregate_member_iter_t iter = {
      .tag_kind = tag_kind,
      .tag_name = tag_name,
      .tag_len = tag_len,
      .tag_scope_depth_p1 = tag_scope_depth_p1,
      .ordinal = 0,
      .count = ctx_get_tag_member_count_scoped(tag_kind, tag_name, tag_len,
                                               tag_scope_depth_p1),
  };
  psx_tag_flat_cover_state_init(&iter.cover_state);
  return iter;
}

static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      tag_member_info_t *out, int *out_ordinal) {
  if (!iter || !out) return 0;
  while (iter->ordinal < iter->count) {
    int ordinal = iter->ordinal++;
    tag_member_info_t mi = {0};
    if (!ctx_get_tag_member_info_scoped(iter->tag_kind, iter->tag_name,
                                        iter->tag_len, iter->tag_scope_depth_p1,
                                        ordinal, &mi)) {
      return 0;
    }
    if (psx_tag_member_is_unnamed_struct(&mi)) continue;
    if (psx_tag_flat_cover_state_covers(&iter->cover_state, &mi)) continue;
    psx_tag_flat_cover_state_note(&iter->cover_state, iter->tag_kind,
                                  iter->tag_name, iter->tag_len, &mi);
    *out = mi;
    if (out_ordinal) *out_ordinal = ordinal;
    return 1;
  }
  return 0;
}

static void gvar_aggregate_member_iter_set_next(gvar_aggregate_member_iter_t *iter,
                                                int next_ordinal) {
  if (!iter) return;
  if (next_ordinal < 0) next_ordinal = 0;
  if (next_ordinal > iter->count) next_ordinal = iter->count;
  iter->ordinal = next_ordinal;
}

static int gvar_walk_require_scalar(const psx_gvar_aggregate_walk_ops_t *ops) {
  return ops && ops->scalar;
}

static int gvar_walk_require_bitfield_unit(const psx_gvar_aggregate_walk_ops_t *ops) {
  return ops && ops->bitfield_unit;
}

static int gvar_walk_require_bitfield_member(const psx_gvar_aggregate_walk_ops_t *ops) {
  return ops && ops->bitfield_member;
}

static void gvar_walk_emit_padding(const psx_gvar_aggregate_walk_ops_t *ops,
                                   void *user, long long offset, int size) {
  if (ops && ops->padding && size > 0) ops->padding(user, offset, size);
}

static int gvar_walk_needs_padding(const psx_gvar_aggregate_walk_ops_t *ops) {
  return ops && ops->padding;
}

static int gvar_walk_struct_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                        int tag_scope_depth_p1,
                                        global_var_t *gv, gvar_init_cursor_t *cur,
                                        long long base_offset, int struct_size,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user) {
  if (!cur) return 1;
  int prev_end = 0;
  gvar_aggregate_member_iter_t iter =
      gvar_aggregate_member_iter(tag_kind, tag_name, tag_len, tag_scope_depth_p1);
  while (gvar_init_cursor_has(cur)) {
    tag_member_info_t mi = {0};
    int ordinal = 0;
    if (!gvar_aggregate_member_next(&iter, &mi, &ordinal)) break;
    if (mi.offset > prev_end) {
      gvar_walk_emit_padding(ops, user, base_offset + prev_end, mi.offset - prev_end);
    }
    if (mi.bit_width > 0) {
      if (!gvar_walk_require_bitfield_unit(ops)) return 0;
      psx_gvar_bitfield_unit_t unit = {0};
      if (!gvar_init_cursor_pack_bitfield_unit(tag_kind, tag_name, tag_len,
                                               ordinal, cur, &unit)) {
        return 0;
      }
      ops->bitfield_unit(user, &unit, base_offset);
      gvar_aggregate_member_iter_set_next(&iter, unit.last_member_index + 1);
      prev_end = unit.offset + unit.size;
      continue;
    }
    int member_value_size = psx_tag_member_decl_value_size(&mi);
    int member_storage_size = psx_tag_member_decl_storage_size(&mi);
    int member_array_count = psx_tag_member_decl_array_count(&mi);
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    psx_tag_member_decl_tag_identity(&mi, &member_tag_kind, &member_tag_name,
                                     &member_tag_len, NULL);
    if (member_array_count > 0) {
      if (psx_tag_member_is_tag_aggregate(&mi)) {
        for (int k = 0; k < member_array_count; k++) {
          if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
          int elem_start_idx = gvar_init_cursor_index(cur);
          long long elem_off = base_offset + mi.offset + (long long)k * member_value_size;
          int ok = psx_tag_member_is_union_aggregate(&mi)
              ? gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                            0,
                                            gv, cur, elem_off, member_value_size,
                                            ops, user)
              : gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                             0,
                                             gv, cur, elem_off, member_value_size,
                                             ops, user);
          if (!ok) return 0;
          gvar_init_cursor_consume_tag_zero_padding(member_tag_kind, member_tag_name,
                                                    member_tag_len, cur,
                                                    elem_start_idx);
        }
      } else {
        if (!gvar_walk_require_scalar(ops)) return 0;
        for (int k = 0; k < member_array_count; k++) {
          long long elem_off = base_offset + mi.offset + (long long)k * member_value_size;
          if (!gvar_init_cursor_has(cur)) {
            if (gvar_walk_needs_padding(ops)) {
              gvar_walk_emit_padding(ops, user, elem_off, member_value_size);
              continue;
            }
            break;
          }
          int slot = gvar_init_cursor_advance(cur);
          ops->scalar(user, &mi, slot, elem_off);
        }
      }
      prev_end = mi.offset + member_storage_size;
      continue;
    }
    if (psx_tag_member_is_struct_aggregate(&mi)) {
      int member_start_idx = gvar_init_cursor_index(cur);
      if (!gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len, 0,
                                        gv, cur, base_offset + mi.offset,
                                        member_value_size,
                                        ops, user)) {
        return 0;
      }
      gvar_init_cursor_consume_tag_zero_padding(member_tag_kind, member_tag_name,
                                                member_tag_len, cur,
                                                member_start_idx);
      prev_end = mi.offset + member_value_size;
      continue;
    }
    if (psx_tag_member_is_union_aggregate(&mi)) {
      if (!gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len, 0,
                                       gv, cur, base_offset + mi.offset,
                                       member_value_size,
                                       ops, user)) {
        return 0;
      }
      prev_end = mi.offset + member_value_size;
      continue;
    }
    if (!gvar_walk_require_scalar(ops)) return 0;
    int slot = gvar_init_cursor_advance(cur);
    ops->scalar(user, &mi, slot, base_offset + mi.offset);
    prev_end = mi.offset + member_value_size;
  }
  if (prev_end < struct_size) {
    gvar_walk_emit_padding(ops, user, base_offset + prev_end, struct_size - prev_end);
  }
  return 1;
}

static int gvar_walk_union_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       int tag_scope_depth_p1,
                                       global_var_t *gv, gvar_init_cursor_t *cur,
                                       long long base_offset, int union_size,
                                       const psx_gvar_aggregate_walk_ops_t *ops,
                                       void *user) {
  if (!gvar_init_cursor_has(cur)) {
    gvar_walk_emit_padding(ops, user, base_offset, union_size);
    return 1;
  }
  int start_idx = gvar_init_cursor_index(cur);
  tag_member_info_t mi = {0};
  if (!tag_union_init_member_for_slot_scoped(tag_kind, tag_name, tag_len,
                                            tag_scope_depth_p1, gv,
                                            gvar_init_cursor_index(cur), &mi)) {
    if (ops && ops->padding) {
      gvar_walk_emit_padding(ops, user, base_offset, union_size);
      return 1;
    }
    return 0;
  }
  int member_value_size = psx_tag_member_decl_value_size(&mi);
  int member_storage_size = psx_tag_member_decl_storage_size(&mi);
  int member_array_count = psx_tag_member_decl_array_count(&mi);
  int emitted = member_array_count > 0 ? member_storage_size : member_value_size;
  token_kind_t member_tag_kind = TK_EOF;
  char *member_tag_name = NULL;
  int member_tag_len = 0;
  psx_tag_member_decl_tag_identity(&mi, &member_tag_kind, &member_tag_name,
                                   &member_tag_len, NULL);
  if (mi.offset > 0) gvar_walk_emit_padding(ops, user, base_offset, mi.offset);
  if (mi.bit_width > 0) {
    if (!gvar_walk_require_bitfield_member(ops)) return 0;
    int slot = gvar_init_cursor_advance(cur);
    ops->bitfield_member(user, &mi, slot, base_offset + mi.offset);
    gvar_init_cursor_consume_tag_zero_padding(tag_kind, tag_name, tag_len,
                                              cur, start_idx);
    if (mi.offset + member_value_size < union_size) {
      gvar_walk_emit_padding(ops, user, base_offset + mi.offset + member_value_size,
                             union_size - (mi.offset + member_value_size));
    }
    return 1;
  }
  if (member_array_count > 0) {
    if (psx_tag_member_is_tag_aggregate(&mi)) {
      for (int k = 0; k < member_array_count; k++) {
        if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
        long long elem_off = base_offset + mi.offset + (long long)k * member_value_size;
        int ok = psx_tag_member_is_struct_aggregate(&mi)
            ? gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                           0,
                                           gv, cur, elem_off, member_value_size,
                                           ops, user)
            : gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                          0,
                                          gv, cur, elem_off, member_value_size,
                                          ops, user);
        if (!ok) return 0;
      }
    } else {
      if (!gvar_walk_require_scalar(ops)) return 0;
      for (int k = 0; k < member_array_count; k++) {
        long long elem_off = base_offset + mi.offset + (long long)k * member_value_size;
        if (!gvar_init_cursor_has(cur)) {
          if (gvar_walk_needs_padding(ops)) {
            gvar_walk_emit_padding(ops, user, elem_off, member_value_size);
            continue;
          }
          break;
        }
        int slot = gvar_init_cursor_advance(cur);
        ops->scalar(user, &mi, slot, elem_off);
      }
    }
    if (mi.offset + emitted < union_size) {
      gvar_walk_emit_padding(ops, user, base_offset + mi.offset + emitted,
                             union_size - (mi.offset + emitted));
    }
    return 1;
  }
  if (psx_tag_member_is_tag_aggregate(&mi)) {
    int ok = psx_tag_member_is_struct_aggregate(&mi)
        ? gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                       0,
                                       gv, cur, base_offset + mi.offset,
                                       member_value_size, ops, user)
        : gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                      0,
                                      gv, cur, base_offset + mi.offset,
                                      member_value_size, ops, user);
    if (!ok) return 0;
    gvar_init_cursor_consume_tag_zero_padding(tag_kind, tag_name, tag_len,
                                              cur, start_idx);
    if (mi.offset + emitted < union_size) {
      gvar_walk_emit_padding(ops, user, base_offset + mi.offset + emitted,
                             union_size - (mi.offset + emitted));
    }
    return 1;
  }
  if (!gvar_walk_require_scalar(ops)) return 0;
  int slot = gvar_init_cursor_advance(cur);
  ops->scalar(user, &mi, slot, base_offset + mi.offset);
  gvar_init_cursor_consume_tag_zero_padding(tag_kind, tag_name, tag_len,
                                            cur, start_idx);
  if (mi.offset + member_value_size < union_size) {
    gvar_walk_emit_padding(ops, user, base_offset + mi.offset + member_value_size,
                           union_size - (mi.offset + member_value_size));
  }
  return 1;
}

int psx_gvar_walk_aggregate_initializer(global_var_t *gv, long long base_offset,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user) {
  if (!psx_gvar_is_tag_aggregate(gv)) return 0;
  gvar_aggregate_layout_t layout = gvar_aggregate_layout(gv);
  gvar_init_cursor_t cur = gvar_init_cursor(gv);
  if (!layout.is_array) {
    return layout.is_union
        ? gvar_walk_union_initializer(layout.tag_kind, layout.tag_name,
                                      layout.tag_len, layout.tag_scope_depth_p1,
                                      gv, &cur, base_offset, layout.type_size,
                                      ops, user)
        : gvar_walk_struct_initializer(layout.tag_kind, layout.tag_name,
                                       layout.tag_len, layout.tag_scope_depth_p1,
                                       gv, &cur, base_offset, layout.type_size,
                                       ops, user);
  }
  for (int e = 0; e < layout.elem_count; e++) {
    if (!gvar_init_cursor_has(&cur) && !gvar_walk_needs_padding(ops)) break;
    long long elem_off = base_offset + (long long)e * layout.elem_size;
    if (layout.is_union) {
      if (!gvar_walk_union_initializer(layout.tag_kind, layout.tag_name,
                                       layout.tag_len, layout.tag_scope_depth_p1,
                                       gv, &cur, elem_off, layout.elem_size,
                                       ops, user)) {
        return 0;
      }
    } else {
      int elem_start_idx = gvar_init_cursor_index(&cur);
      if (!gvar_walk_struct_initializer(layout.tag_kind, layout.tag_name,
                                        layout.tag_len, layout.tag_scope_depth_p1,
                                        gv, &cur, elem_off, layout.elem_size,
                                        ops, user)) {
        return 0;
      }
      gvar_init_cursor_consume_tag_zero_padding(layout.tag_kind, layout.tag_name,
                                                layout.tag_len, &cur, elem_start_idx);
    }
  }
  return 1;
}

int psx_gvar_initializer_element_size(const global_var_t *gv, int fallback_size) {
  if (psx_gvar_is_array(gv)) {
    psx_type_t *type = gvar_decl_type_view(gv);
    int leaf_elem = type_array_leaf_element_size(type);
    if (leaf_elem > 0) return leaf_elem;
    int elem = psx_gvar_array_element_size(gv);
    if (elem > 0) return elem;
  }
  return fallback_size;
}

int psx_gvar_initializer_element_count(const global_var_t *gv, int fallback_size) {
  if (gv && !psx_gvar_is_array(gv)) return gv->has_init ? 1 : 0;
  int elem = psx_gvar_initializer_element_size(gv, fallback_size);
  psx_type_t *type = gvar_decl_type_view(gv);
  int size = psx_type_sizeof(type);
  if (size <= 0) size = psx_gvar_storage_size(gv, fallback_size);
  return elem > 0 ? (size + elem - 1) / elem : 0;
}

psx_gvar_init_slot_t psx_gvar_init_slot_view(const global_var_t *gv, int idx) {
  psx_gvar_init_slot_t slot = {0};
  if (!gv || idx < 0 || idx >= gv->init_count) return slot;
  slot.in_range = 1;
  slot.symbol = gv->init_value_symbols ? gv->init_value_symbols[idx] : NULL;
  slot.symbol_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[idx] : 0;
  slot.value = gv->init_values ? gv->init_values[idx] : 0;
  slot.fvalue = gv->init_fvalues ? gv->init_fvalues[idx] : 0.0;
  if (!slot.symbol) {
    if (slot.symbol_len == -2) slot.fp_sentinel_kind = TK_FLOAT_KIND_FLOAT;
    else if (slot.symbol_len == -3) slot.fp_sentinel_kind = TK_FLOAT_KIND_DOUBLE;
  }
  return slot;
}

static gvar_init_cursor_t gvar_init_cursor(const global_var_t *gv) {
  psx_gvar_view_t view = psx_gvar_view(gv);
  return (gvar_init_cursor_t){
      .gv = gv,
      .index = 0,
      .count = view.init_count,
  };
}

static int gvar_init_cursor_has(const gvar_init_cursor_t *cur) {
  return cur && cur->index < cur->count;
}

static int gvar_init_cursor_index(const gvar_init_cursor_t *cur) {
  return cur ? cur->index : 0;
}

static int gvar_init_cursor_advance(gvar_init_cursor_t *cur) {
  if (!gvar_init_cursor_has(cur)) return -1;
  return cur->index++;
}

static int gvar_init_cursor_consume_plain_zero_padding(gvar_init_cursor_t *cur,
                                                       int start_idx, int target_slots) {
  if (!cur || target_slots <= 1) return 0;
  int limit = start_idx + target_slots;
  int consumed = 0;
  while (cur->index < limit && gvar_init_cursor_has(cur) &&
         psx_gvar_init_slot_is_plain_zero(cur->gv, cur->index)) {
    cur->index++;
    consumed++;
  }
  return consumed;
}

static int gvar_init_cursor_consume_tag_zero_padding(token_kind_t tag_kind, char *tag_name,
                                                     int tag_len,
                                                     gvar_init_cursor_t *cur,
                                                     int start_idx) {
  return gvar_init_cursor_consume_plain_zero_padding(
      cur, start_idx, psx_tag_flat_slot_count(tag_kind, tag_name, tag_len));
}

unsigned long long psx_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset) {
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, idx);
  unsigned long long mask = bit_width >= 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
  return ((unsigned long long)slot.value & mask) << bit_offset;
}

static int gvar_init_cursor_pack_bitfield_unit(token_kind_t tag_kind, char *tag_name,
                                               int tag_len, int member_index,
                                               gvar_init_cursor_t *cur,
                                               psx_gvar_bitfield_unit_t *out) {
  if (!cur || !out) return 0;
  tag_member_info_t first = {0};
  if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, member_index, &first) ||
      first.bit_width <= 0) {
    return 0;
  }
  int n_members = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int unit_off = first.offset;
  int unit_size = psx_tag_member_decl_value_size(&first);
  unsigned long long packed = 0;
  int m = member_index;
  int last = member_index;
  while (m < n_members && gvar_init_cursor_has(cur)) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, m, &mi)) break;
    if (mi.bit_width <= 0 || mi.offset != unit_off) break;
    packed |= psx_gvar_init_slot_bitfield_bits(cur->gv, cur->index,
                                               mi.bit_width, mi.bit_offset);
    gvar_init_cursor_advance(cur);
    last = m;
    m++;
  }
  *out = (psx_gvar_bitfield_unit_t){
      .offset = unit_off,
      .size = unit_size,
      .last_member_index = last,
      .packed = packed,
  };
  return 1;
}

void psx_gvar_init_slots_alloc(global_var_t *gv, int cap, int with_fvalues) {
  if (!gv || cap <= 0) return;
  gv->init_values = calloc((size_t)cap, sizeof(long long));
  gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
  gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
  gv->init_union_ordinals = malloc((size_t)cap * sizeof(int));
  if (with_fvalues) gv->init_fvalues = calloc((size_t)cap, sizeof(double));
  for (int i = 0; i < cap; i++) psx_gvar_init_slot_set_ordinal(gv, i, -1);
}

void psx_gvar_init_slots_ensure_capacity(global_var_t *gv, int *cap, int min_cap) {
  if (!gv || !cap) return;
  while (*cap < min_cap) {
    int old_cap = *cap;
    int new_cap = old_cap > 0 ? old_cap * 2 : 1;
    if (new_cap < min_cap) new_cap = min_cap;
    gv->init_values = realloc(gv->init_values, (size_t)new_cap * sizeof(long long));
    gv->init_value_symbols = realloc(gv->init_value_symbols, (size_t)new_cap * sizeof(char *));
    gv->init_value_symbol_lens = realloc(gv->init_value_symbol_lens, (size_t)new_cap * sizeof(int));
    gv->init_union_ordinals = realloc(gv->init_union_ordinals, (size_t)new_cap * sizeof(int));
    if (gv->init_fvalues) {
      gv->init_fvalues = realloc(gv->init_fvalues, (size_t)new_cap * sizeof(double));
    }
    for (int i = old_cap; i < new_cap; i++) {
      psx_gvar_init_slot_clear(gv, i);
    }
    *cap = new_cap;
  }
}

void psx_gvar_init_slots_pad_zeros(global_var_t *gv, int *cap, int total_slots) {
  if (!gv || !cap) return;
  psx_gvar_init_slots_ensure_capacity(gv, cap, total_slots);
  while (gv->init_count < total_slots) {
    psx_gvar_init_slot_clear(gv, gv->init_count);
    gv->init_count++;
  }
}

typedef struct {
  global_var_t *gv;
  int idx;
} gvar_string_units_write_ctx_t;

static void write_gvar_string_unit(uint32_t unit, void *user) {
  gvar_string_units_write_ctx_t *ctx = user;
  psx_gvar_init_slot_write(ctx->gv, ctx->idx++, (long long)unit, 0.0, NULL, 0);
}

int psx_gvar_init_slots_write_string_units(global_var_t *gv, int start_idx,
                                           const char *str, int len,
                                           int elem_size, int max_slots) {
  if (!gv || !str || start_idx < 0 || len < 0 || elem_size <= 0 || max_slots <= 0) {
    return start_idx;
  }
  gvar_string_units_write_ctx_t ctx = {gv, start_idx};
  tk_emit_string_code_units(str, len, elem_size, max_slots,
                            write_gvar_string_unit, &ctx);
  return ctx.idx;
}

void psx_gvar_init_slot_clear(global_var_t *gv, int idx) {
  if (!gv || idx < 0) return;
  if (gv->init_values) gv->init_values[idx] = 0;
  if (gv->init_value_symbols) gv->init_value_symbols[idx] = NULL;
  if (gv->init_value_symbol_lens) gv->init_value_symbol_lens[idx] = 0;
  if (gv->init_union_ordinals) gv->init_union_ordinals[idx] = -1;
  if (gv->init_fvalues) gv->init_fvalues[idx] = 0.0;
}

void psx_gvar_init_slot_write(global_var_t *gv, int idx, long long value,
                              double fvalue, char *symbol, int symbol_len) {
  if (!gv || idx < 0) return;
  if (gv->init_values) gv->init_values[idx] = value;
  if (gv->init_value_symbols) gv->init_value_symbols[idx] = symbol;
  if (gv->init_value_symbol_lens) gv->init_value_symbol_lens[idx] = symbol_len;
  if (gv->init_fvalues) gv->init_fvalues[idx] = fvalue;
}

void psx_gvar_init_slot_write_fp_sentinel(global_var_t *gv, int idx,
                                          tk_float_kind_t fp_kind, int fp_size) {
  if (!gv || idx < 0 || fp_kind == TK_FLOAT_KIND_NONE) return;
  if (gv->init_value_symbols) gv->init_value_symbols[idx] = NULL;
  if (gv->init_value_symbol_lens) gv->init_value_symbol_lens[idx] = (fp_size >= 8) ? -3 : -2;
}

void psx_gvar_init_slot_set_ordinal(global_var_t *gv, int idx, int ordinal) {
  if (!gv || idx < 0 || !gv->init_union_ordinals) return;
  gv->init_union_ordinals[idx] = ordinal;
}

tk_float_kind_t psx_gvar_init_slot_fp_kind(const global_var_t *gv, int idx) {
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, idx);
  if (slot.fp_sentinel_kind != TK_FLOAT_KIND_NONE) return slot.fp_sentinel_kind;
  return TK_FLOAT_KIND_NONE;
}

int psx_gvar_init_slot_is_plain_zero(const global_var_t *gv, int idx) {
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, idx);
  if (!slot.in_range) return 1;
  return slot.symbol == NULL && slot.symbol_len == 0 && slot.value == 0 && slot.fvalue == 0.0;
}

int psx_gvar_union_init_slot_fp_size(const global_var_t *gv, int idx) {
  tk_float_kind_t fp_kind = psx_gvar_init_slot_fp_kind(gv, idx);
  if (fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 0;
}

int psx_gvar_union_init_slot_ordinal(const global_var_t *gv, int idx) {
  if (!gv) return -1;
  if (idx >= 0 && idx < gv->init_count &&
      gv->init_union_ordinals && gv->init_union_ordinals[idx] >= 0) {
    return gv->init_union_ordinals[idx];
  }
  return gv->union_init_ordinal;
}

static int tag_member_fp_size(const tag_member_info_t *mi) {
  tk_float_kind_t fp_kind = psx_tag_member_decl_fp_kind(mi);
  return fp_kind == TK_FLOAT_KIND_FLOAT ? 4
       : fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 0;
}

static const psx_type_t *tag_member_direct_tag_leaf_from_type(const tag_member_info_t *mi) {
  const psx_type_t *type = psx_tag_member_decl_type(mi);
  if (!type || type->kind == PSX_TYPE_POINTER) return NULL;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return psx_type_is_tag_aggregate(type) ? type : NULL;
}

int psx_tag_member_is_struct_aggregate(const tag_member_info_t *mi) {
  const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
  if (leaf) return leaf->kind == PSX_TYPE_STRUCT;
  if (psx_tag_member_decl_type(mi)) return 0;
  return mi && !mi->is_tag_pointer && mi->tag_kind == TK_STRUCT;
}

int psx_tag_member_is_union_aggregate(const tag_member_info_t *mi) {
  const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
  if (leaf) return leaf->kind == PSX_TYPE_UNION;
  if (psx_tag_member_decl_type(mi)) return 0;
  return mi && !mi->is_tag_pointer && mi->tag_kind == TK_UNION;
}

int psx_tag_member_is_tag_aggregate(const tag_member_info_t *mi) {
  return psx_tag_member_is_struct_aggregate(mi) ||
         psx_tag_member_is_union_aggregate(mi);
}

int psx_tag_member_is_unnamed_struct(const tag_member_info_t *mi) {
  return mi && mi->len == 0 && psx_tag_member_is_struct_aggregate(mi);
}

int psx_tag_member_is_unnamed_union(const tag_member_info_t *mi) {
  return mi && mi->len == 0 && psx_tag_member_is_union_aggregate(mi);
}

int psx_tag_member_is_unnamed_aggregate(const tag_member_info_t *mi) {
  return psx_tag_member_is_unnamed_struct(mi) ||
         psx_tag_member_is_unnamed_union(mi);
}

void psx_tag_flat_cover_state_init(psx_tag_flat_cover_state_t *state) {
  if (!state) return;
  state->covered_union_off = 0;
  state->covered_union_size = 0;
}

int psx_tag_flat_cover_state_covers(const psx_tag_flat_cover_state_t *state,
                                    const tag_member_info_t *mi) {
  if (!state || !mi || state->covered_union_size <= 0) return 0;
  return mi->offset >= state->covered_union_off &&
         mi->offset < state->covered_union_off + state->covered_union_size;
}

void psx_tag_flat_cover_state_note(psx_tag_flat_cover_state_t *state,
                                   token_kind_t tag_kind, char *tag_name, int tag_len,
                                   const tag_member_info_t *mi) {
  if (!state || !mi) return;
  if (psx_tag_member_is_unnamed_union(mi)) {
    state->covered_union_off = mi->offset;
    state->covered_union_size = psx_tag_member_decl_storage_size(mi);
    return;
  }
  int cover_off = 0;
  int cover_size = 0;
  if (psx_tag_find_unnamed_union_covering_offset(tag_kind, tag_name, tag_len,
                                                 0, mi->offset,
                                                 &cover_off, &cover_size)) {
    state->covered_union_off = cover_off;
    state->covered_union_size = cover_size;
  }
}

int psx_tag_find_unnamed_union_covering_offset(token_kind_t tag_kind, char *tag_name, int tag_len,
                                               int base_off, int target_off,
                                               int *out_off, int *out_size) {
  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (!psx_tag_member_is_unnamed_aggregate(&mi)) continue;
    int start = base_off + mi.offset;
    int member_storage_size = psx_tag_member_decl_storage_size(&mi);
    int end = start + member_storage_size;
    if (target_off < start || target_off >= end) continue;
    if (psx_tag_member_is_union_aggregate(&mi)) {
      if (out_off) *out_off = start;
      if (out_size) *out_size = member_storage_size;
      return 1;
    }
    if (psx_tag_member_is_struct_aggregate(&mi)) {
      token_kind_t child_kind = TK_EOF;
      char *child_name = NULL;
      int child_len = 0;
      psx_tag_member_decl_tag_identity(&mi, &child_kind, &child_name, &child_len, NULL);
      if (psx_tag_find_unnamed_union_covering_offset(child_kind, child_name, child_len,
                                                     start, target_off, out_off, out_size)) {
        return 1;
      }
    }
  }
  return 0;
}

int psx_tag_member_flat_slots(const tag_member_info_t *mi) {
  if (psx_tag_member_is_unnamed_struct(mi)) return 0;
  int per = 1;
  if (psx_tag_member_is_tag_aggregate(mi)) {
    const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
    token_kind_t tag_kind = leaf ? leaf->tag_kind : mi->tag_kind;
    char *tag_name = leaf ? leaf->tag_name : mi->tag_name;
    int tag_len = leaf ? leaf->tag_len : mi->tag_len;
    per = psx_tag_flat_slot_count(tag_kind, tag_name, tag_len);
  }
  int count = psx_tag_member_decl_array_count(mi);
  return count > 0 ? count * per : per;
}

int psx_tag_member_elem_flat_slots(const tag_member_info_t *mi) {
  if (!mi) return 1;
  int total = psx_tag_member_flat_slots(mi);
  int count = psx_tag_member_decl_array_count(mi);
  if (count > 0) {
    int per = total / count;
    return per > 0 ? per : 1;
  }
  return total > 0 ? total : 1;
}

int psx_tag_member_subscript_stride_slots(const tag_member_info_t *mi) {
  int per = psx_tag_member_elem_flat_slots(mi);
  if (!mi || mi->arr_ndim <= 1) return per;
  for (int i = 1; i < mi->arr_ndim; i++) {
    int dim = mi->arr_dims[i];
    if (dim > 0) per *= dim;
  }
  return per > 0 ? per : 1;
}

int psx_tag_flat_slot_count(token_kind_t tag_kind, char *tag_name, int tag_len) {
  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int slots = 0;
  int union_max_bytes = 0;
  psx_tag_flat_cover_state_t cover_state;
  psx_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (tag_kind == TK_UNION) {
      int ms = psx_tag_member_flat_slots(&mi);
      int bytes = psx_tag_member_decl_storage_size(&mi);
      if (bytes > union_max_bytes || (bytes == union_max_bytes && ms > slots)) {
        union_max_bytes = bytes;
        slots = ms;
      }
      continue;
    }
    if (psx_tag_member_is_unnamed_struct(&mi)) continue;
    if (psx_tag_flat_cover_state_covers(&cover_state, &mi)) continue;
    slots += psx_tag_member_flat_slots(&mi);
    psx_tag_flat_cover_state_note(&cover_state, tag_kind, tag_name, tag_len, &mi);
  }
  return slots > 0 ? slots : 1;
}

int psx_tag_member_at_flat_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                int flat_slot, tag_member_info_t *out, int *out_ordinal) {
  if (flat_slot < 0) return 0;
  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int slot = 0;
  psx_tag_flat_cover_state_t cover_state;
  psx_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (psx_tag_member_is_unnamed_struct(&mi)) continue;
    if (psx_tag_flat_cover_state_covers(&cover_state, &mi)) continue;
    int member_slots = psx_tag_member_flat_slots(&mi);
    if (flat_slot < slot + member_slots) {
      if (out) *out = mi;
      if (out_ordinal) *out_ordinal = i;
      return 1;
    }
    psx_tag_flat_cover_state_note(&cover_state, tag_kind, tag_name, tag_len, &mi);
    slot += member_slots;
  }
  return 0;
}

int psx_tag_next_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                              int *ordinal_inout, tag_member_info_t *out) {
  if (!ordinal_inout) return 0;
  int ordinal = *ordinal_inout;
  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  while (ordinal < n) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, ordinal, &mi)) {
      *ordinal_inout = ordinal + 1;
      return 0;
    }
    ordinal++;
    if (mi.len <= 0) continue;
    if (out) *out = mi;
    *ordinal_inout = ordinal;
    return 1;
  }
  *ordinal_inout = ordinal;
  return 0;
}

int psx_tag_first_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                               tag_member_info_t *out, int *out_ordinal) {
  int ordinal = 0;
  if (!psx_tag_next_named_member(tag_kind, tag_name, tag_len, &ordinal, out)) return 0;
  if (out_ordinal) *out_ordinal = ordinal - 1;
  return 1;
}

int psx_tag_find_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                              char *member_name, int member_len,
                              tag_member_info_t *out, int *out_ordinal) {
  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (mi.len == member_len && mi.name &&
        strncmp(mi.name, member_name, (size_t)member_len) == 0) {
      if (out) *out = mi;
      if (out_ordinal) *out_ordinal = i;
      return 1;
    }
  }
  return 0;
}

int psx_tag_select_union_member_for_init_slot(token_kind_t tag_kind, char *tag_name,
                                              int tag_len, const global_var_t *gv,
                                              int idx, tag_member_info_t *mi) {
  if (!mi) return 0;
  int init_fp_size = psx_gvar_union_init_slot_fp_size(gv, idx);
  int selected_fp_size = tag_member_fp_size(mi);
  if (init_fp_size == selected_fp_size) return 0;
  if (init_fp_size == 0 && selected_fp_size == 0) return 0;

  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    tag_member_info_t cand = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &cand)) break;
    int cand_fp_size = tag_member_fp_size(&cand);
    if ((init_fp_size > 0 && cand_fp_size == init_fp_size) ||
        (init_fp_size == 0 && cand_fp_size == 0)) {
      *mi = cand;
      return 1;
    }
  }
  return 0;
}

int psx_tag_union_init_member_for_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       const global_var_t *gv, int idx,
                                       tag_member_info_t *out) {
  return tag_union_init_member_for_slot_scoped(tag_kind, tag_name, tag_len, 0,
                                              gv, idx, out);
}

static int tag_union_init_member_for_slot_scoped(token_kind_t tag_kind, char *tag_name,
                                                int tag_len, int tag_scope_depth_p1,
                                                const global_var_t *gv, int idx,
                                                tag_member_info_t *out) {
  if (!out) return 0;
  int ordinal = psx_gvar_union_init_slot_ordinal(gv, idx);
  if (!ctx_get_tag_member_info_scoped(tag_kind, tag_name, tag_len,
                                      tag_scope_depth_p1, ordinal, out)) return 0;
  psx_tag_select_union_member_for_init_slot(tag_kind, tag_name, tag_len, gv, idx, out);
  return 1;
}

int psx_tag_member_designator_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                   char *member_name, int member_len, int *out_ordinal) {
  int n = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int slot = 0;
  int covered_union_slot = -1;
  int covered_union_off = 0;
  int covered_union_size = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    int in_covered_union = covered_union_slot >= 0 &&
                           mi.offset >= covered_union_off &&
                           mi.offset < covered_union_off + covered_union_size;
    if (mi.len == member_len && mi.name &&
        strncmp(mi.name, member_name, (size_t)member_len) == 0) {
      if (out_ordinal) *out_ordinal = i;
      if (in_covered_union) return covered_union_slot;
      return tag_kind == TK_UNION ? 0 : slot;
    }
    if (psx_tag_member_is_unnamed_struct(&mi)) continue;
    if (psx_tag_member_is_unnamed_union(&mi)) {
      covered_union_slot = slot;
      covered_union_off = mi.offset;
      covered_union_size = psx_tag_member_decl_storage_size(&mi);
      slot += psx_tag_member_flat_slots(&mi);
      continue;
    }
    if (in_covered_union) continue;
    int cover_off = 0;
    int cover_size = 0;
    int has_cover = psx_tag_find_unnamed_union_covering_offset(tag_kind, tag_name, tag_len,
                                                               0, mi.offset,
                                                               &cover_off, &cover_size);
    if (has_cover) {
      covered_union_slot = slot;
      covered_union_off = cover_off;
      covered_union_size = cover_size;
    }
    slot += psx_tag_member_flat_slots(&mi);
  }
  return -1;
}

static int gvar_pointee_is_bool(const global_var_t *gv) {
  psx_type_t *type = (gv && gv->decl_type) ? gvar_decl_type_view(gv) : NULL;
  if (type) {
    const psx_type_t *pointee = type_pointee_value_type(type);
    return pointee && pointee->kind == PSX_TYPE_BOOL ? 1 : 0;
  }
  return gv && (gv->pointee_is_bool || gv->elem_is_bool) ? 1 : 0;
}

static int gvar_pointee_is_unsigned(const global_var_t *gv) {
  psx_type_t *type = (gv && gv->decl_type) ? gvar_decl_type_view(gv) : NULL;
  if (type) {
    const psx_type_t *pointee = type_pointee_value_type(type);
    return pointee && psx_type_is_unsigned(pointee) ? 1 : 0;
  }
  return gv && (gv->pointee_is_unsigned || (gv->is_array && gv->is_unsigned)) ? 1 : 0;
}

static void init_gvar_decl_shape(legacy_decl_shape_t *mem, global_var_t *gv) {
  *mem = (legacy_decl_shape_t){0};
  if (!gv) return;
  mem->fp_kind = (tk_float_kind_t)gv->fp_kind;
  mem->type_size = (short)gv->type_size;
  mem->deref_size = gv->deref_size;
  mem->base_deref_size = gv->pointee_elem_size > 0 ? gv->pointee_elem_size : gv->deref_size;
  mem->tag_kind = gv->tag_kind;
  mem->tag_name = gv->tag_name;
  mem->tag_len = gv->tag_len;
  mem->tag_scope_depth_p1 = gv->tag_scope_depth_p1;
  mem->is_tag_pointer = (gv->is_tag_pointer && (gv->is_array || gv->type_size == 8) &&
                         gv->ptr_array_pointee_bytes <= 0) ? 1 : 0;
  if (gv->is_tag_pointer && !gvar_is_pointer_like_from_fields(gv)) {
    mem->tag_kind = TK_EOF;
    mem->tag_name = NULL;
    mem->tag_len = 0;
    mem->tag_scope_depth_p1 = 0;
  }
  mem->is_pointer = gvar_is_pointer_like_from_fields(gv) ? 1 : 0;
  mem->is_unsigned = gv->is_unsigned ? 1 : 0;
  mem->is_const_qualified = gv->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = gv->is_volatile_qualified ? 1 : 0;
  mem->is_bool = gv->is_bool ? 1 : 0;
  mem->is_long_double = gv->is_long_double ? 1 : 0;
  mem->pointee_is_bool = gvar_pointee_is_bool(gv);
  mem->pointee_is_unsigned = gvar_pointee_is_unsigned(gv);
  mem->pointee_fp_kind = (unsigned int)gv->pointee_fp_kind;
  if (gv->is_array && gv->fp_kind != TK_FLOAT_KIND_NONE) {
    mem->pointee_fp_kind = (unsigned int)gv->fp_kind;
  }
  psx_decl_funcptr_sig_t funcptr_sig =
      psx_decl_funcptr_sig_clone(gv->funcptr_sig);
  legacy_decl_shape_store_funcptr_signature(mem, &funcptr_sig);
  mem->pointer_qual_levels = gv->pointer_qual_levels;
  mem->inner_deref_size = (short)gv->outer_stride;
  mem->next_deref_size = (short)gv->mid_stride;
  mem->extra_strides_count = gv->extra_strides_count;
  for (int i = 0; i < gv->extra_strides_count && i < 5; i++) {
    mem->extra_strides[i] = gv->extra_strides[i];
  }
  mem->ptr_array_pointee_bytes = gv->ptr_array_pointee_bytes;
  if (gv->pointee_elem_size > 0 && gv->tag_kind == TK_EOF &&
      (gv->is_array ||
       (gv->ptr_array_pointee_bytes > 0 && gv->pointer_qual_levels > 0 &&
        gv->deref_size > gv->pointee_elem_size))) {
    mem->pointee_is_scalar_ptr = 1;
  }
}

psx_type_t *psx_gvar_get_decl_type(global_var_t *gv) {
  return gvar_decl_type_consistent(gv);
}

psx_type_t *psx_gvar_materialize_decl_type(global_var_t *gv) {
  if (!gv) return NULL;
  if (gv->decl_type) {
    if (gv->ptr_array_pointee_bytes <= 0)
      sync_materialized_gvar_runtime_shape(gv, gv->decl_type);
    if (gv->type_sig) gv->decl_type->type_sig = gv->type_sig;
    return gv->decl_type;
  }
  legacy_decl_shape_t mem;
  init_gvar_decl_shape(&mem, gv);
  psx_type_t *arena_type = type_from_legacy_decl_shape(&mem, gv->is_array, 0);
  psx_type_canonicalize_flat_pointer_to_array(arena_type);
  gv->decl_type = psx_type_clone_persistent(arena_type);
  sync_materialized_gvar_runtime_shape(gv, gv->decl_type);
  if (gv->decl_type) gv->decl_type->type_sig = gv->type_sig;
  return gv->decl_type;
}

static psx_type_t *type_new_void(void) {
  psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
  type->scalar_kind = TK_VOID;
  return type;
}

static int integer_token_size(token_kind_t kind, int fallback_size) {
  switch (kind) {
    case TK_BOOL:
    case TK_CHAR: return 1;
    case TK_SHORT: return 2;
    case TK_LONG: return 8;
    case TK_INT:
    case TK_UNSIGNED:
      return 4;
    default:
      return fallback_size > 0 ? fallback_size : 4;
  }
}

static psx_type_t *type_from_scalar_shape(token_kind_t kind, tk_float_kind_t fp_kind,
                                          int size, int is_unsigned, int is_complex,
                                          int is_long_long) {
  if (kind == TK_VOID) return type_new_void();
  if (is_complex) {
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = fp_kind;
    type->size = size > 0 ? size : 16;
    type->align = type->size >= 8 ? 8 : 4;
    return type;
  }
  if (fp_kind == TK_FLOAT_KIND_FLOAT)
    return psx_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE)
    return psx_type_new_float(fp_kind, 8);
  psx_type_t *type = psx_type_new_integer(kind == TK_BOOL ? TK_BOOL : kind,
                                          integer_token_size(kind, size),
                                          is_unsigned);
  type->is_long_long = is_long_long ? 1 : 0;
  return type;
}

static void type_apply_pointee_qualifiers(psx_type_t *type,
                                          int is_const_qualified,
                                          int is_volatile_qualified) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base) return;
  if (is_const_qualified) type->base->is_const_qualified = 1;
  if (is_volatile_qualified) type->base->is_volatile_qualified = 1;
  psx_type_t *leaf = type->base;
  while (leaf && leaf->kind == PSX_TYPE_ARRAY && leaf->base) leaf = leaf->base;
  if (leaf && leaf != type->base) {
    if (is_const_qualified) leaf->is_const_qualified = 1;
    if (is_volatile_qualified) leaf->is_volatile_qualified = 1;
  }
}

static psx_type_t *type_from_direct_funcall(node_func_t *fn) {
  if (!fn || fn->callee != NULL || !fn->funcname) return NULL;
  psx_type_t *direct_ret_type =
      type_clone_arena(psx_ctx_get_function_ret_type(fn->funcname, fn->funcname_len));
  if (direct_ret_type) return direct_ret_type;
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(fn->funcname, fn->funcname_len);
  tk_float_kind_t ret_fp_kind = ret.fp_kind;
  if (ret_fp_kind == TK_FLOAT_KIND_NONE) {
    if (ret.token_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (ret.token_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  }
  int size = ret.struct_size > 0 ? ret.struct_size : integer_token_size(ret.token_kind, 4);

  if (!ret.is_pointer) {
    if (psx_ctx_is_tag_aggregate_kind(ret.tag_kind))
      return psx_type_new_tag(ret.tag_kind, ret.tag_name, ret.tag_len, 0, ret.struct_size);
    return type_from_scalar_shape(ret.token_kind, ret_fp_kind, size,
                                  ret.is_unsigned, ret.is_complex, 0);
  }

  psx_type_t *base = NULL;
  if (psx_ctx_is_tag_aggregate_kind(ret.tag_kind)) {
    base = psx_type_new_tag(ret.tag_kind, ret.tag_name, ret.tag_len, 0, ret.struct_size);
  } else {
    base = type_from_scalar_shape(ret.token_kind, ret_fp_kind, size,
                                  ret.is_unsigned, ret.is_complex, 0);
  }
  psx_ret_pointee_array_t ret_array = ret.pointee_array;
  ret_array.elem_size = psx_type_sizeof(base);
  int levels = ret.pointer_levels;
  psx_type_t *pointee = psx_type_wrap_ret_pointee_array_base(base, ret_array);
  int deref_size = levels >= 2 ? 8 : psx_type_sizeof(pointee);
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    int row = psx_ret_pointee_array_row_stride(ret_array);
    if (row > 0) deref_size = row;
  }
  psx_type_t *type =
      psx_type_wrap_pointer_levels(pointee, levels, deref_size,
                                   psx_type_sizeof(base), 0, 0);
  type->base_deref_size = psx_type_sizeof(base);
  type_apply_pointee_qualifiers(type, ret.pointee_const_qualified,
                                ret.pointee_volatile_qualified);
  if (ret.is_funcptr) {
    type->funcptr_sig = psx_decl_funcptr_sig_clone(ret.funcptr_sig);
  }
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    type->funcptr_sig.function.callable.return_shape.pointee_array = ret_array;
    psx_type_sync_pointer_to_array_metadata_from_base(type);
  }
  return type;
}

static psx_funcptr_signature_t funcptr_signature_from_function_name(char *name, int len) {
  psx_funcptr_signature_t suffix = {0};
  suffix.is_variadic =
      psx_ctx_get_function_is_variadic(name, len, &suffix.nargs_fixed) ? 1 : 0;
  for (int i = 0; i < suffix.nargs_fixed && i < 16; i++) {
    tk_float_kind_t fp_kind = psx_ctx_get_function_param_fp_kind(name, len, i);
    if (fp_kind != TK_FLOAT_KIND_NONE) {
      unsigned short fp_code = fp_kind == TK_FLOAT_KIND_FLOAT ? 1u : 2u;
      suffix.param_fp_mask |= (unsigned short)(fp_code << (2 * i));
      continue;
    }
    int param_size = psx_ctx_get_function_param_int_size(name, len, i);
    if (param_size > 0) {
      unsigned short int_code = param_size >= 8 ? 2u : 1u;
      suffix.param_int_mask |= (unsigned short)(int_code << (2 * i));
      continue;
    }
    if (psx_ctx_get_function_param_category(name, len, i) == PSX_PCAT_PTR) {
      suffix.param_int_mask |= (unsigned short)(3u << (2 * i));
    }
  }
  return suffix;
}

static psx_type_t *type_from_funcref(node_funcref_t *fr) {
  if (!fr || !fr->funcname) return NULL;
  psx_function_ret_info_t ret =
      psx_ctx_get_function_ret_info(fr->funcname, fr->funcname_len);
  psx_type_t *ctx_ret_type =
      type_clone_arena(psx_ctx_get_function_ret_type(fr->funcname, fr->funcname_len));
  tk_float_kind_t ret_fp_kind = ret.fp_kind;
  if (ret_fp_kind == TK_FLOAT_KIND_NONE) {
    if (ret.token_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (ret.token_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  }
  int size = ret.struct_size > 0 ? ret.struct_size : integer_token_size(ret.token_kind, 4);
  psx_type_t *base = ctx_ret_type;
  if (!base) {
    if (psx_ctx_is_tag_aggregate_kind(ret.tag_kind)) {
      base = psx_type_new_tag(ret.tag_kind, ret.tag_name, ret.tag_len, 0, ret.struct_size);
    } else {
      base = type_from_scalar_shape(ret.token_kind, ret_fp_kind, size,
                                    ret.is_unsigned, ret.is_complex, 0);
    }
  }
  int deref_size = psx_type_sizeof(base);
  if (deref_size <= 0 && ret.token_kind == TK_VOID) deref_size = 8;
  if (deref_size <= 0) deref_size = 4;

  psx_funcptr_signature_t suffix =
      funcptr_signature_from_function_name(fr->funcname, fr->funcname_len);
  psx_ret_pointee_array_t ret_array = ret.pointee_array;
  ret_array.elem_size = ret.is_pointer ? psx_type_sizeof(base) : 0;
  psx_decl_funcptr_sig_t sig = psx_decl_make_funcptr_sig_from_kind(
      &suffix, ret.token_kind, ret_fp_kind, ret.is_pointer, ret.is_funcptr,
      ret.is_complex, ret_array);
  if (ret.is_funcptr) {
    sig = funcptr_sig_merge_missing(sig, &ret.funcptr_sig, 1);
    sig.function.returned_funcptr =
        psx_funcptr_returned_func_mark(sig.function.returned_funcptr);
  }

  psx_type_t *type = psx_type_new_pointer(base, deref_size);
  type->pointer_qual_levels = 1;
  type->base_deref_size = deref_size;
  type->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  type->tag_kind = ret.tag_kind;
  type->tag_name = ret.tag_name;
  type->tag_len = ret.tag_len;
  return type;
}

static int tag_type_from_type(psx_type_t *type, token_kind_t *tag_kind, char **tag_name,
                              int *tag_len, int *is_tag_pointer,
                              int *tag_scope_depth_p1) {
  if (!type) return 0;
  psx_type_t *tag_type = NULL;
  int ptr = 0;
  psx_type_t *cur = type;
  while (cur && (cur->kind == PSX_TYPE_POINTER || cur->kind == PSX_TYPE_ARRAY)) {
    ptr = 1;
    cur = cur->base;
    if (psx_type_is_tag_aggregate(cur)) {
      tag_type = cur;
      break;
    }
  }
  if (!tag_type && psx_type_is_tag_aggregate(type)) {
    tag_type = type;
  }
  if (!tag_type && psx_ctx_is_tag_aggregate_kind(type->tag_kind)) {
    tag_type = type;
    ptr = psx_type_is_pointer(type);
  }
  if (!tag_type || !psx_ctx_is_tag_aggregate_kind(tag_type->tag_kind))
    return 0;
  if (tag_kind) *tag_kind = tag_type->tag_kind;
  if (tag_name) *tag_name = tag_type->tag_name;
  if (tag_len) *tag_len = tag_type->tag_len;
  if (is_tag_pointer) *is_tag_pointer = ptr;
  if (tag_scope_depth_p1) *tag_scope_depth_p1 = tag_type->tag_scope_depth_p1;
  return 1;
}

static node_tag_view_t node_tag_view_zero(void) {
  return (node_tag_view_t){TK_EOF, NULL, 0, 0, 0};
}

static int tag_view_from_type(psx_type_t *type, node_tag_view_t *view) {
  node_tag_view_t out = node_tag_view_zero();
  if (!type) {
    if (view) *view = out;
    return 0;
  }
  tag_type_from_type(type, &out.kind, &out.name, &out.len, &out.is_pointer,
                     &out.scope_depth_p1);
  if (view) *view = out;
  return 1;
}

static int tag_view_from_node_direct(node_t *node, node_tag_view_t *view) {
  if (!node) {
    if (view) *view = node_tag_view_zero();
    return 0;
  }
  node_tag_view_t typed = node_tag_view_zero();
  if (tag_view_from_type(psx_node_get_type(node), &typed) &&
      typed.kind != TK_EOF) {
    if (view) *view = typed;
    return 1;
  }
  if (view) *view = typed;
  return 0;
}

static psx_type_t *type_from_funcptr_callee_type(node_func_t *fn) {
  if (!fn || !fn->callee) return NULL;
  psx_type_t *callee_type = psx_node_get_type(fn->callee);
  if (!callee_type || callee_type->kind != PSX_TYPE_POINTER) return NULL;
  psx_decl_funcptr_sig_t callee_sig = funcptr_sig_from_type(callee_type);
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int ignored_ptr = 0;
  tag_type_from_type(callee_type, &tag_kind, &tag_name, &tag_len, &ignored_ptr, NULL);
  int has_tag_value_return =
      !callee_sig.function.callable.return_shape.is_data_pointer && psx_ctx_is_tag_aggregate_kind(tag_kind);
  if (!has_tag_value_return && !funcptr_sig_has_return_shape(callee_sig)) return NULL;

  if (callee_sig.function.callable.return_shape.is_void) return type_new_void();
  if (callee_sig.function.callable.return_shape.is_complex) {
    int complex_size =
        callee_sig.function.callable.return_shape.fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, callee_sig.function.callable.return_shape.fp_kind,
                                  complex_size, 0, 1, 0);
  }

  psx_ret_pointee_array_t ret_array = callee_sig.function.callable.return_shape.pointee_array;
  if (!callee_sig.function.callable.return_shape.is_data_pointer &&
      psx_ctx_is_tag_aggregate_kind(tag_kind)) {
    int size = callee_type->base ? psx_type_sizeof(callee_type->base) : 0;
    if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_type_new_tag(tag_kind, tag_name, tag_len,
                            callee_type->base ? callee_type->base->tag_scope_depth_p1 : 0,
                            size);
  }

  if (callee_sig.function.callable.return_shape.is_data_pointer || psx_ret_pointee_array_has_dims(ret_array)) {
    psx_type_t *base = NULL;
    if (psx_ctx_is_tag_aggregate_kind(tag_kind)) {
      int size = callee_type->base ? psx_type_sizeof(callee_type->base) : 0;
      if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      base = psx_type_new_tag(tag_kind, tag_name, tag_len,
                              callee_type->base ? callee_type->base->tag_scope_depth_p1 : 0,
                              size);
    } else if (callee_sig.function.callable.return_shape.pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float(callee_sig.function.callable.return_shape.pointee_fp_kind,
                                callee_sig.function.callable.return_shape.pointee_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    } else {
      int base_size = callee_sig.function.callable.return_shape.int_width > 0
                          ? callee_sig.function.callable.return_shape.int_width
                          : ret_array.elem_size;
      if (base_size <= 0) {
        base_size = callee_type->base_deref_size > 0
                        ? callee_type->base_deref_size
                        : callee_type->deref_size;
      }
      if (base_size <= 0 || base_size > 8) base_size = 4;
      base = psx_type_new_integer(TK_EOF, base_size,
                                  callee_type->base && callee_type->base->is_unsigned);
    }
    int deref_size = psx_type_sizeof(base);
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      int row = psx_ret_pointee_array_row_stride(ret_array);
      if (row > 0) deref_size = row;
    }
    psx_type_t *pointee = psx_type_wrap_ret_pointee_array_base(base, ret_array);
    psx_type_t *type = psx_type_new_pointer(pointee, deref_size);
    if (callee_type->base) {
      type_apply_pointee_qualifiers(type, callee_type->base->is_const_qualified,
                                    callee_type->base->is_volatile_qualified);
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->funcptr_sig.function.callable.return_shape.pointee_array = ret_array;
      psx_type_sync_pointer_to_array_metadata_from_base(type);
    }
    type->base_deref_size = psx_type_sizeof(base);
    type->pointer_qual_levels = 1;
    return type;
  }

  if (callee_sig.function.callable.return_shape.fp_kind != TK_FLOAT_KIND_NONE) {
    return psx_type_new_float(callee_sig.function.callable.return_shape.fp_kind,
                              callee_sig.function.callable.return_shape.fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  int width = callee_sig.function.callable.return_shape.int_width;
  if (width <= 0) width = 4;
  return psx_type_new_integer(TK_EOF, width,
                              callee_type->base && callee_type->base->is_unsigned);
}

static psx_type_t *type_from_indirect_funcall(node_func_t *fn) {
  return type_from_funcptr_callee_type(fn);
}

static void sync_funcall_result_metadata_from_type(node_func_t *fn,
                                                   const psx_type_t *type) {
  if (!fn || !type) return;
  fn->base.fp_kind = TK_FLOAT_KIND_NONE;
  fn->base.is_complex = 0;
  fn->base.is_void_call = 0;
  fn->base.ret_struct_size = 0;
  fn->base.is_unsigned = 0;
  fn->base.is_atomic = type->is_atomic ? 1 : 0;
  fn->base.is_long_long = 0;

  if (type->kind == PSX_TYPE_VOID) {
    fn->base.is_void_call = 1;
    return;
  }
  if (type->kind == PSX_TYPE_COMPLEX) {
    fn->base.fp_kind = type->fp_kind != TK_FLOAT_KIND_NONE
                           ? type->fp_kind
                           : TK_FLOAT_KIND_DOUBLE;
    fn->base.is_complex = 1;
    return;
  }
  if (type->kind == PSX_TYPE_FLOAT) {
    fn->base.fp_kind = type->fp_kind;
    return;
  }
  if (psx_type_is_tag_aggregate(type)) {
    int size = psx_type_sizeof(type);
    if (size > 0) fn->base.ret_struct_size = size;
    return;
  }
  if (type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER) {
    fn->base.is_unsigned = psx_type_is_unsigned(type) ? 1 : 0;
    fn->base.is_long_long = type->is_long_long ? 1 : 0;
  }
}

static int node_uses_plain_scalar_result_metadata(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
    case ND_TERNARY:
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
    case ND_INT_TO_FP:
    case ND_FNEG:
    case ND_CREAL:
    case ND_CIMAG:
    case ND_DEREF:
      return 1;
    default:
      return 0;
  }
}

static void sync_plain_scalar_result_metadata_from_type(node_t *node,
                                                        const psx_type_t *type) {
  if (!node || !type || !node_uses_plain_scalar_result_metadata(node)) return;
  node->fp_kind = TK_FLOAT_KIND_NONE;
  node->is_complex = 0;
  node->is_unsigned = 0;
  node->is_atomic = type->is_atomic ? 1 : 0;
  node->is_long_long = 0;

  if (type->kind == PSX_TYPE_COMPLEX) {
    node->fp_kind = type->fp_kind != TK_FLOAT_KIND_NONE
                        ? type->fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    node->is_complex = 1;
    return;
  }
  if (type->kind == PSX_TYPE_FLOAT) {
    node->fp_kind = type->fp_kind;
    return;
  }
  if (type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER) {
    node->is_unsigned = psx_type_is_unsigned(type) ? 1 : 0;
    node->is_long_long = type->is_long_long ? 1 : 0;
  }
}

static int type_is_integer_like(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER;
}

static int type_integer_promotion_size(const psx_type_t *type) {
  int size = psx_type_sizeof(type);
  if (size <= 0) return 4;
  return size < 4 ? 4 : size;
}

static int type_uac_effective_unsigned(const psx_type_t *type) {
  if (!type_is_integer_like(type)) return 0;
  int original_size = psx_type_sizeof(type);
  return original_size >= 4 && psx_type_is_unsigned(type);
}

static psx_type_t *type_usual_arith_result(psx_type_t *lhs_type, psx_type_t *rhs_type,
                                           tk_float_kind_t fp_kind, int is_complex) {
  int result_is_complex =
      is_complex ||
      (lhs_type && lhs_type->kind == PSX_TYPE_COMPLEX) ||
      (rhs_type && rhs_type->kind == PSX_TYPE_COMPLEX);
  if (result_is_complex) {
    if (lhs_type && lhs_type->fp_kind > fp_kind) fp_kind = lhs_type->fp_kind;
    if (rhs_type && rhs_type->fp_kind > fp_kind) fp_kind = rhs_type->fp_kind;
    if (fp_kind == TK_FLOAT_KIND_NONE) fp_kind = TK_FLOAT_KIND_DOUBLE;
    int size = psx_type_sizeof(lhs_type);
    int rhs_size = psx_type_sizeof(rhs_type);
    if (rhs_size > size) size = rhs_size;
    if (size <= 0) size = fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, fp_kind, size, 0, 1, 0);
  }

  if ((lhs_type && lhs_type->kind == PSX_TYPE_FLOAT) ||
      (rhs_type && rhs_type->kind == PSX_TYPE_FLOAT) ||
      fp_kind != TK_FLOAT_KIND_NONE) {
    tk_float_kind_t fp = fp_kind;
    if (lhs_type && lhs_type->fp_kind > fp) fp = lhs_type->fp_kind;
    if (rhs_type && rhs_type->fp_kind > fp) fp = rhs_type->fp_kind;
    if (fp == TK_FLOAT_KIND_NONE) fp = TK_FLOAT_KIND_DOUBLE;
    psx_type_t *type = psx_type_new_float(fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    if ((lhs_type && lhs_type->is_long_double) ||
        (rhs_type && rhs_type->is_long_double)) {
      type->is_long_double = 1;
    }
    return type;
  }

  int lsize = type_integer_promotion_size(lhs_type);
  int rsize = type_integer_promotion_size(rhs_type);
  int lunsigned = type_uac_effective_unsigned(lhs_type);
  int rhs_unsigned = type_uac_effective_unsigned(rhs_type);
  int is_unsigned;
  if (lunsigned == rhs_unsigned) {
    is_unsigned = lunsigned;
  } else {
    int unsigned_w = lunsigned ? lsize : rsize;
    int signed_w = lunsigned ? rsize : lsize;
    is_unsigned = unsigned_w >= signed_w;
  }
  int size = lsize > rsize ? lsize : rsize;
  int is_long_long = (lhs_type && lhs_type->is_long_long) ||
                     (rhs_type && rhs_type->is_long_long);
  psx_type_t *type = psx_type_new_integer(TK_EOF, size, is_unsigned);
  type->is_long_long = is_long_long ? 1 : 0;
  return type;
}

static psx_type_t *type_from_operand_usual_arith(node_t *lhs, node_t *rhs) {
  tk_float_kind_t fp = psx_node_value_fp_kind(lhs);
  tk_float_kind_t rhs_fp = psx_node_value_fp_kind(rhs);
  if (rhs_fp > fp) fp = rhs_fp;
  return type_usual_arith_result(psx_node_get_type(lhs), psx_node_get_type(rhs), fp,
                                 psx_node_value_is_complex(lhs) ||
                                     psx_node_value_is_complex(rhs));
}

static int type_result_unsigned(const psx_type_t *type) {
  return type && type->kind != PSX_TYPE_POINTER && psx_type_is_unsigned(type);
}

static psx_type_t *type_from_binary_expr(node_t *node) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
      return psx_type_new_integer(TK_INT, 4, 0);
    case ND_SHL:
    case ND_SHR:
      return psx_node_get_type(node->lhs);
    case ND_ADD:
    case ND_SUB:
      if (ps_node_is_pointer(node)) {
        if (ps_node_is_pointer(node->lhs)) return psx_node_get_type(node->lhs);
        return psx_node_get_type(node->rhs);
      }
      /* fallthrough */
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR: {
      return type_from_operand_usual_arith(node->lhs, node->rhs);
    }
    default:
      return NULL;
  }
}

static psx_type_t *type_from_ternary_expr(node_t *node) {
  node_ctrl_t *ctrl = (node_ctrl_t *)node;
  if (!ctrl) return NULL;
  if (ps_node_is_pointer(ctrl->base.rhs)) return psx_node_get_type(ctrl->base.rhs);
  if (ps_node_is_pointer(ctrl->els)) return psx_node_get_type(ctrl->els);
  psx_type_t *then_type = psx_node_get_type(ctrl->base.rhs);
  psx_type_t *else_type = psx_node_get_type(ctrl->els);
  if (then_type && else_type &&
      then_type->kind == else_type->kind &&
      psx_type_is_tag_aggregate(then_type)) {
    return then_type;
  }
  tk_float_kind_t fp = psx_node_value_fp_kind(ctrl->base.rhs);
  tk_float_kind_t else_fp = psx_node_value_fp_kind(ctrl->els);
  if (else_fp > fp) fp = else_fp;
  int is_complex = psx_node_value_is_complex(ctrl->base.rhs) ||
                   psx_node_value_is_complex(ctrl->els);
  if (fp == TK_FLOAT_KIND_NONE && !is_complex && !then_type && !else_type) {
    fp = (tk_float_kind_t)node->fp_kind;
    is_complex = node->is_complex ? 1 : 0;
  }
  return type_usual_arith_result(then_type, else_type, fp, is_complex);
}

psx_type_t *psx_node_get_type(node_t *node) {
  if (!node) return NULL;
  if (node->type) {
    if (node->kind == ND_FUNCALL || node->kind == ND_FUNCDEF)
      sync_funcall_result_metadata_from_type((node_func_t *)node, node->type);
    else
      sync_plain_scalar_result_metadata_from_type(node, node->type);
    return node->type;
  }
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
    {
      psx_type_t *type = psx_node_get_type(node->rhs);
      sync_plain_scalar_result_metadata_from_type(node, type);
      return node->type = type;
    }
    case ND_TERNARY:
    {
      psx_type_t *type = type_from_ternary_expr(node);
      sync_plain_scalar_result_metadata_from_type(node, type);
      return node->type = type;
    }
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR:
    case ND_SHL:
    case ND_SHR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
    {
      psx_type_t *type = type_from_binary_expr(node);
      sync_plain_scalar_result_metadata_from_type(node, type);
      return node->type = type;
    }
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return node->type = psx_node_get_type(node->lhs);
    case ND_FUNCALL: {
      psx_type_t *type = type_from_direct_funcall((node_func_t *)node);
      if (!type) type = type_from_indirect_funcall((node_func_t *)node);
      sync_funcall_result_metadata_from_type((node_func_t *)node, type);
      return node->type = type;
    }
    case ND_FUNCDEF: {
      psx_type_t *type = type_from_direct_funcall((node_func_t *)node);
      sync_funcall_result_metadata_from_type((node_func_t *)node, type);
      return node->type = type;
    }
    case ND_FUNCREF:
      return node->type = type_from_funcref((node_funcref_t *)node);
    case ND_INT_TO_FP:
    case ND_FNEG:
    case ND_CREAL:
    case ND_CIMAG:
    {
      psx_type_t *type = psx_type_new_float((tk_float_kind_t)node->fp_kind,
                                            node->fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
      sync_plain_scalar_result_metadata_from_type(node, type);
      return node->type = type;
    }
    case ND_NUM: {
      node_num_t *num = (node_num_t *)node;
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT)
        return node->type = psx_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
      if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE)
        return node->type = psx_type_new_float((tk_float_kind_t)node->fp_kind, 8);
      int sz = num->int_width == 1 || num->int_width == 2 ? num->int_width
               : (num->int_is_long ? 8 : 4);
      psx_type_t *type = psx_type_new_integer(TK_EOF, sz, node->is_unsigned);
      type->is_long_long = num->int_is_long_long;
      type->is_plain_char = num->int_is_plain_char;
      return node->type = type;
    }
    default:
      return NULL;
  }
}

psx_type_t *psx_node_materialize_type(node_t *node) {
  if (!node) return NULL;
  psx_type_t *type = psx_node_get_type(node);
  if (type) node->type = type;
  return type;
}

int ps_node_type_size(node_t *node) {
  if (!node) return 0;
  int canonical_size = psx_type_sizeof(psx_node_get_type(node));
  if (canonical_size > 0) return canonical_size;
  switch (node->kind) {
    case ND_LVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST: {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_TYPE_SIZE, &value);
      return value;
    }
    case ND_COMMA:
      return ps_node_type_size(node->rhs);
    case ND_STMT_EXPR:
      return ps_node_type_size(node->rhs);
    case ND_TERNARY: {
      int s = psx_type_sizeof(psx_node_get_type(node));
      return s > 0 ? s : 4;
    }
    case ND_FUNCALL: {
      if (node->ret_struct_size > 0) return node->ret_struct_size;
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
      if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
      return 4;
    }
    /* 算術/論理演算: ポインタ算術 (ptr ± int) なら 8、それ以外は
     * C11 6.3.1.8 通常算術変換に従い、両オペランドのうち広い方を返す。
     * ND_NUM のように type_size を持たないノードでは 0 が返るので、int (4) に
     * 落とす。`sizeof(a+b)` や `sizeof(n++)` で 8 になる誤りを防ぐ。 */
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR: {
      int s = psx_type_sizeof(psx_node_get_type(node));
      return s > 0 ? s : 4;
    }
    case ND_SHL:
    case ND_SHR: {
      int s = psx_type_sizeof(psx_node_get_type(node));
      return s > 0 ? s : 4;
    }
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC: {
      int s = ps_node_type_size(node->lhs);
      return s > 0 ? s : 4;
    }
    case ND_FP_TO_INT:
    case ND_INT_TO_FP:
    case ND_FNEG:
    case ND_CREAL:
    case ND_CIMAG: {
      int s = psx_type_sizeof(psx_node_get_type(node));
      return s > 0 ? s : 0;
    }
    case ND_LT: case ND_LE:
    case ND_EQ: case ND_NE:
    case ND_LOGAND: case ND_LOGOR:
      return 4; /* 比較/論理結果は int (C11 6.5.8/9) */
    case ND_NUM: {
      /* 整数/浮動小数リテラルの型サイズ。従来 0 を返し sizeof_expr_node の既定 8 に
       * 落ちて `sizeof(0)`/`sizeof(1L+2)` が誤っていた。fp_kind と long サフィックスで
       * 判定する (int=4, long/long long=8, float=4, double=8)。 */
      node_num_t *n = (node_num_t *)node;
      if (n->base.fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
      if (n->base.fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
      if (n->int_width == 1 || n->int_width == 2) return n->int_width;
      return n->int_is_long ? 8 : 4;
    }
    default:
      return 0;
  }
}

int psx_node_storage_type_size(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  int s = psx_type_sizeof(type);
  if (node->type) return s;
  if (s > 0) return s;
  return ps_node_type_size(node);
}

int ps_node_deref_size(node_t *node) {
  if (!node) return 0;
  if (node->type) {
    int value = 0;
    if (node_value_view_from_node_direct(node, NODE_VALUE_DEREF_SIZE, &value))
      return value;
  }
  switch (node->kind) {
    case ND_LVAR: {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_DEREF_SIZE, &value);
      return value;
    }
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
    {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_DEREF_SIZE, &value);
      return value;
    }
    case ND_COMMA:
    case ND_STMT_EXPR:
      return ps_node_deref_size(node->rhs);
    /* 条件演算子: ポインタ側分岐の deref_size を引き継ぐ
     * (`(c ? p : q)[i]` の要素サイズ決定に必要)。 */
    case ND_TERNARY: {
      int l = ps_node_deref_size(node->rhs);
      if (l > 0) return l;
      return ps_node_deref_size(((node_ctrl_t *)node)->els);
    }
    /* ND_ADD/SUB の結果がポインタなら、ポインタ側の deref_size を引き継ぐ。 */
    case ND_ADD:
    case ND_SUB: {
      int l = ps_node_deref_size(node->lhs);
      if (l > 0) return l;
      return ps_node_deref_size(node->rhs);
    }
    /* `p++` 等の inc/dec はオペランドの deref_size をそのまま継承する。
     * `*p++` で deref のロード幅 (= pointee サイズ) を正しく決めるのに必要。 */
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return ps_node_deref_size(node->lhs);
    /* ポインタ戻り値の関数 `int *g(); g()[i]` / `g()+i`: pointee サイズを返さないと
     * 添字/ポインタ算術がスケールせず 1 バイト加算になる (miscompile/SIGSEGV)。
     * 配列へのポインタ戻り `int (*f())[N]` では pointee は配列 (N*base) なので行ストライドを返す。 */
    case ND_FUNCALL: {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_DEREF_SIZE, &value);
      return value;
    }
    default:
      return 0;
  }
}

int ps_node_is_pointer(node_t *node) {
  if (!node) return 0;
  if (node->type) {
    int value = 0;
    node_value_view_from_node_direct(node, NODE_VALUE_IS_POINTER, &value);
    return value;
  }
  switch (node->kind) {
    case ND_LVAR: {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_IS_POINTER, &value);
      return value;
    }
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST: {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_IS_POINTER, &value);
      return value;
    }
    case ND_COMMA:
    case ND_STMT_EXPR:
      return ps_node_is_pointer(node->rhs);
    /* C11 6.5.15: 条件演算子の結果は両オペランドがポインタなら
     * ポインタ。`(c ? p : q)[i]` の subscript 判定で必要。 */
    case ND_TERNARY:
      return ps_node_is_pointer(node->rhs) ||
             ps_node_is_pointer(((node_ctrl_t *)node)->els);
    /* C11 6.5.6: ポインタ + 整数 / 整数 + ポインタ / ポインタ - 整数 の結果
     * もポインタ。新規 ND_ADD/SUB ノードに is_pointer 属性を直接書けない
     * (psx_node_new_binary は node_t を作る) ので、子を見て判定する。 */
    case ND_ADD:
      return ps_node_is_pointer(node->lhs) || ps_node_is_pointer(node->rhs);
    case ND_SUB:
      /* ポインタ - ポインタ は ptrdiff_t (整数) なので除外。
       * ポインタ - 整数 のみポインタ扱い。 */
      if (ps_node_is_pointer(node->lhs) && ps_node_is_pointer(node->rhs)) return 0;
      return ps_node_is_pointer(node->lhs);
    case ND_FUNCALL:
    {
      int value = 0;
      node_value_view_from_node_direct(node, NODE_VALUE_IS_POINTER, &value);
      return value;
    }
    case ND_FUNCREF:
      /* 関数シンボルは関数ポインタ値。 */
      return 1;
    default:
      return 0;
  }
}

int psx_node_pointer_qual_levels(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (pointer_view_from_node_direct(node, NODE_POINTER_QUAL_LEVELS, &value))
    return value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointer_qual_levels(node->rhs);
    /* ポインタ算術 `pp + n` / `pp - n`: ポインタ側 (通常 lhs、稀に rhs) の pql を carry。
     * これがないと `*(pp + n)` の build_unary_deref_node で pql=0 になり、`struct P **pp;
     * (*(pp + 2))->m` の中間 deref が struct ポインタとして認識されず E3005 になっていた。 */
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_pointer_qual_levels(node->lhs);
      if (l > 0) return l;
      return psx_node_pointer_qual_levels(node->rhs);
    }
    default:
      return 0;
  }
}

int psx_node_base_deref_size(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (pointer_view_from_node_direct(node, NODE_POINTER_BASE_DEREF_SIZE, &value))
    return value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_base_deref_size(node->rhs);
    /* ポインタ算術 `pp + n` / `pp - n`: ポインタ側 (通常 lhs、稀に rhs) の bds を carry。
     * pql と対称で多段ポインタ算術の最終 deref が正しい load 幅を使えるようにする。 */
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_base_deref_size(node->lhs);
      if (l > 0) return l;
      return psx_node_base_deref_size(node->rhs);
    }
    default:
      return 0;
  }
}

int psx_node_ptr_array_pointee_bytes(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (pointer_view_from_node_direct(node, NODE_POINTER_PTR_ARRAY_POINTEE_BYTES,
                                    &value))
    return value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_ptr_array_pointee_bytes(node->rhs);
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_ptr_array_pointee_bytes(node->lhs);
      if (l > 0) return l;
      return psx_node_ptr_array_pointee_bytes(node->rhs);
    }
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_ptr_array_pointee_bytes(node->lhs);
    case ND_FUNCALL: {
      if (pointer_view_from_node_direct(node, NODE_POINTER_PTR_ARRAY_POINTEE_BYTES,
                                        &value))
        return value;
      return 0;
    }
    default: {
      pointer_view_from_node_direct(node, NODE_POINTER_PTR_ARRAY_POINTEE_BYTES, &value);
      return value;
    }
  }
}

static int node_has_explicit_type(node_t *node) {
  return node && node->type ? 1 : 0;
}

static void node_type_state_store_stride(node_t *node, int inner_stride,
                                         int next_stride,
                                         const int *extra_strides,
                                         int extra_count) {
  if (!node) return;
  if (extra_count < 0) extra_count = 0;
  if (extra_count > 5) extra_count = 5;
  node->type_state.inner_stride = inner_stride;
  node->type_state.next_stride = next_stride;
  node->type_state.extra_strides_count = (unsigned char)extra_count;
  for (int i = 0; i < extra_count; i++)
    node->type_state.extra_strides[i] = extra_strides ? extra_strides[i] : 0;
  for (int i = extra_count; i < 5; i++) node->type_state.extra_strides[i] = 0;
  node->type_state.has_stride =
      inner_stride > 0 || next_stride > 0 || extra_count > 0;
}

static int node_type_state_stride(const node_t *node, int *inner_stride,
                                  int *next_stride, int *extra_strides,
                                  int *extra_strides_count) {
  if (!node || !node->type_state.has_stride) return 0;
  int count = node->type_state.extra_strides_count;
  if (count > 5) count = 5;
  if (inner_stride) *inner_stride = node->type_state.inner_stride;
  if (next_stride) *next_stride = node->type_state.next_stride;
  if (extra_strides_count) *extra_strides_count = count;
  if (extra_strides) {
    for (int i = 0; i < count; i++)
      extra_strides[i] = node->type_state.extra_strides[i];
    for (int i = count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
}

static int type_is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

static int type_is_pointer_to_array_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER &&
         type->base && type->base->kind == PSX_TYPE_ARRAY;
}

static psx_type_t *type_array_with_pointer_element_storage(psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY ||
      !type->base || type->base->kind != PSX_TYPE_POINTER) {
    return type;
  }
  int pointer_size = psx_type_sizeof(type->base);
  if (pointer_size <= 0) pointer_size = 8;
  if (type->elem_size == pointer_size && type->deref_size == pointer_size) {
    return type;
  }
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->elem_size = pointer_size;
  copy->deref_size = pointer_size;
  return copy;
}

static tk_float_kind_t type_deep_pointee_fp_kind(const psx_type_t *type) {
  if (!type_is_pointer_view_type(type)) return TK_FLOAT_KIND_NONE;
  const psx_type_t *cur = type;
  int missing_base = 0;
  while (type_is_pointer_view_type(cur)) {
    if (!cur->base) {
      missing_base = 1;
      break;
    }
    cur = cur->base;
  }
  if (cur && cur->kind == PSX_TYPE_FLOAT) return cur->fp_kind;
  if (!missing_base) return TK_FLOAT_KIND_NONE;

  cur = type;
  while (type_is_pointer_view_type(cur)) {
    if (cur->pointee_fp_kind != TK_FLOAT_KIND_NONE) return cur->pointee_fp_kind;
    cur = cur->base;
  }
  return TK_FLOAT_KIND_NONE;
}

static int scalar_flag_from_type(const psx_type_t *type, node_scalar_flag_t flag) {
  if (!type || type_is_pointer_view_type(type)) return 0;
  switch (flag) {
    case NODE_SCALAR_UNSIGNED:
      return psx_type_is_unsigned(type);
    case NODE_SCALAR_LONG_LONG:
      return type->is_long_long ? 1 : 0;
    case NODE_SCALAR_PLAIN_CHAR:
      return type->is_plain_char ? 1 : 0;
    case NODE_SCALAR_LONG_DOUBLE:
      return type->is_long_double ? 1 : 0;
    default:
      return 0;
  }
}

static int scalar_flag_from_node_fallback(node_t *node, node_scalar_flag_t flag) {
  switch (flag) {
    case NODE_SCALAR_UNSIGNED:
      return node && node->is_unsigned ? 1 : 0;
    case NODE_SCALAR_LONG_LONG:
      return node && node->is_long_long ? 1 : 0;
    default:
      return 0;
  }
}

static int scalar_flag_from_node_direct(node_t *node, node_scalar_flag_t flag) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node_has_explicit_type(node)) return scalar_flag_from_type(type, flag);
  if (scalar_flag_from_type(type, flag)) return 1;
  return scalar_flag_from_node_fallback(node, flag);
}

static int pointee_flag_from_type(const psx_type_t *type, node_pointee_flag_t flag) {
  const psx_type_t *base = type_pointee_value_type(type);
  if (!base) return 0;
  switch (flag) {
    case NODE_POINTEE_UNSIGNED:
      return psx_type_is_unsigned(base);
    case NODE_POINTEE_BOOL:
      return base->kind == PSX_TYPE_BOOL;
    case NODE_POINTEE_VOID:
      return base->kind == PSX_TYPE_VOID;
    case NODE_POINTEE_CONST:
      return base->is_const_qualified ? 1 : 0;
    case NODE_POINTEE_VOLATILE:
      return base->is_volatile_qualified ? 1 : 0;
    default:
      return 0;
  }
}

static int pointee_flag_from_node_direct(node_t *node, node_pointee_flag_t flag) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, flag);
  return 0;
}

static int pointer_view_from_type(const psx_type_t *type, node_pointer_view_field_t field,
                                  int *value) {
  if (!type_is_pointer_view_type(type)) return 0;
  switch (field) {
    case NODE_POINTER_QUAL_LEVELS:
      {
        int levels = psx_type_pointer_view_structural_qual_levels(type);
        if (levels <= 0) return 0;
        if (value) *value = levels;
      }
      return 1;
    case NODE_POINTER_BASE_DEREF_SIZE:
      {
        int base_deref_size =
            psx_type_pointer_view_structural_base_deref_size(type);
        if (base_deref_size <= 0) return 0;
        if (value) *value = base_deref_size;
      }
      return 1;
    case NODE_POINTER_PTR_ARRAY_POINTEE_BYTES:
      {
        int bytes = psx_type_pointer_view_structural_ptr_array_pointee_bytes(type);
        if (bytes <= 0) return 0;
        if (value) *value = bytes;
      }
      return 1;
    case NODE_POINTER_CONST_MASK:
      if (value)
        *value = (int)psx_type_pointer_view_structural_qual_mask(type, 0);
      return 1;
    case NODE_POINTER_VOLATILE_MASK:
      if (value)
        *value = (int)psx_type_pointer_view_structural_qual_mask(type, 1);
      return 1;
    case NODE_POINTER_POINTEE_FP_KIND:
      {
        tk_float_kind_t fp = type_deep_pointee_fp_kind(type);
        if (fp == TK_FLOAT_KIND_NONE) return 0;
        if (value) *value = (int)fp;
      }
      return 1;
    default:
      return 0;
  }
}

static int pointer_view_from_node_direct(node_t *node, node_pointer_view_field_t field,
                                         int *value) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (pointer_view_from_type(type, field, value)) return 1;
  return 0;
}

static int vla_view_from_type(const psx_type_t *type, node_vla_view_field_t field,
                              int *value) {
  if (!type_is_pointer_view_type(type)) return 0;
  switch (field) {
    case NODE_VLA_ROW_STRIDE_FRAME_OFF:
      {
        int row_stride_frame_off =
            psx_type_pointer_view_vla_row_stride_frame_off(type);
        if (row_stride_frame_off == 0) return 0;
        if (value) *value = row_stride_frame_off;
      }
      return 1;
    case NODE_VLA_STRIDES_REMAINING:
      {
        int strides_remaining =
            psx_type_pointer_view_vla_strides_remaining(type);
        if (strides_remaining <= 0) return 0;
        if (value) *value = strides_remaining;
      }
      return 1;
    default:
      return 0;
  }
}

static int vla_view_from_node_direct(node_t *node, node_vla_view_field_t field,
                                     int *value) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (vla_view_from_type(type, field, value)) return 1;
  return 0;
}

static int node_value_view_from_type(const psx_type_t *type, node_value_view_field_t field,
                                     int *value, int require_positive) {
  if (!type) return 0;
  int v = 0;
  switch (field) {
    case NODE_VALUE_TYPE_SIZE:
      v = psx_type_sizeof(type);
      break;
    case NODE_VALUE_DEREF_SIZE:
      v = psx_type_deref_size(type);
      break;
    case NODE_VALUE_IS_POINTER:
      v = psx_type_is_pointer(type) ? 1 : 0;
      require_positive = 0;
      break;
    default:
      return 0;
  }
  if (require_positive && v <= 0) return 0;
  if (value) *value = v;
  return 1;
}

static int node_value_view_from_node_direct(node_t *node, node_value_view_field_t field,
                                            int *value) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node->type) {
    if (field == NODE_VALUE_DEREF_SIZE &&
        node->type_state.has_deref_size) {
      if (value) *value = node->type_state.deref_size;
      return 1;
    }
    if (node_value_view_from_type(type, field, value,
                                  field == NODE_VALUE_TYPE_SIZE))
      return 1;
    return 0;
  }
  if (field == NODE_VALUE_IS_POINTER) {
    if (type) {
      if (value) *value = psx_type_is_pointer(type) ? 1 : 0;
      return 1;
    }
    return 0;
  }
  if (node_value_view_from_type(type, field, value,
                                1))
    return 1;
  return 0;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_node(node_t *node, int copy_variadic) {
  if (!node) return (psx_decl_funcptr_sig_t){0};
  psx_decl_funcptr_sig_t sig = {0};
  psx_type_t *type = psx_node_get_type(node);
  if (type) sig = funcptr_sig_merge_missing(sig, &type->funcptr_sig, copy_variadic);
  return sig;
}

int psx_node_has_funcptr_signature(node_t *node) {
  if (!node) return 0;
  return psx_decl_funcptr_sig_has_payload(funcptr_sig_from_node(node, 1));
}

psx_decl_funcptr_sig_t psx_node_funcptr_sig(node_t *node) {
  return funcptr_sig_from_node(node, 1);
}

psx_decl_funcptr_sig_t psx_lvar_funcptr_sig(const lvar_t *src) {
  return funcptr_sig_from_lvar(src);
}

psx_decl_funcptr_sig_t psx_gvar_funcptr_sig(const global_var_t *src) {
  return funcptr_sig_from_gvar(src);
}

psx_decl_funcptr_sig_t psx_gvar_funcptr_sig_by_name(char *name, int len) {
  return psx_gvar_funcptr_sig(psx_find_global_var(name, len));
}

unsigned short psx_node_funcptr_param_fp_mask(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).function.callable.signature.param_fp_mask;
}

unsigned short psx_node_funcptr_param_int_mask(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).function.callable.signature.param_int_mask;
}

int psx_node_funcptr_returns_void(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).function.callable.return_shape.is_void ? 1 : 0;
}

int psx_node_funcptr_returns_complex(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).function.callable.return_shape.is_complex ? 1 : 0;
}

int psx_node_funcptr_returns_pointee_array(node_t *node) {
  if (!node) return 0;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_node(node, 1);
  return psx_ret_pointee_array_has_dims(sig.function.callable.return_shape.pointee_array) ? 1 : 0;
}

tk_float_kind_t psx_node_funcptr_ret_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  return funcptr_sig_from_node(node, 1).function.callable.return_shape.fp_kind;
}

static global_var_t *static_local_backing_gvar(const lvar_t *var) {
  if (!var || !var->static_global_name) return NULL;
  return psx_find_global_var(var->static_global_name,
                             var->static_global_name_len);
}

static psx_type_t *static_local_backing_decl_type(const lvar_t *var) {
  global_var_t *backing = static_local_backing_gvar(var);
  return backing ? psx_gvar_get_decl_type(backing) : NULL;
}

unsigned int psx_node_pointer_const_qual_mask(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (pointer_view_from_node_direct(node, NODE_POINTER_CONST_MASK, &value))
    return (unsigned int)value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointer_const_qual_mask(node->rhs);
    case ND_TERNARY: {
      unsigned int r = psx_node_pointer_const_qual_mask(node->rhs);
      if (r) return r;
      return psx_node_pointer_const_qual_mask(((node_ctrl_t *)node)->els);
    }
    case ND_ADD:
    case ND_SUB: {
      unsigned int l = psx_node_pointer_const_qual_mask(node->lhs);
      if (l) return l;
      return psx_node_pointer_const_qual_mask(node->rhs);
    }
    default:
      return 0;
  }
}

unsigned int psx_node_pointer_volatile_qual_mask(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (pointer_view_from_node_direct(node, NODE_POINTER_VOLATILE_MASK, &value))
    return (unsigned int)value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointer_volatile_qual_mask(node->rhs);
    case ND_TERNARY: {
      unsigned int r = psx_node_pointer_volatile_qual_mask(node->rhs);
      if (r) return r;
      return psx_node_pointer_volatile_qual_mask(((node_ctrl_t *)node)->els);
    }
    case ND_ADD:
    case ND_SUB: {
      unsigned int l = psx_node_pointer_volatile_qual_mask(node->lhs);
      if (l) return l;
      return psx_node_pointer_volatile_qual_mask(node->rhs);
    }
    default:
      return 0;
  }
}

int psx_node_pointee_is_unsigned(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, NODE_POINTEE_UNSIGNED);
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointee_is_unsigned(node->rhs);
    case ND_ADD:
    case ND_SUB:
      return psx_node_pointee_is_unsigned(node->lhs) ||
             psx_node_pointee_is_unsigned(node->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_is_unsigned(node->lhs);
    default:
      return pointee_flag_from_node_direct(node, NODE_POINTEE_UNSIGNED);
  }
}

int psx_node_atomic_pointer_info(node_t *ptr_arg, int *width, int *is_unsigned) {
  int w = ps_node_deref_size(ptr_arg);
  if (w != 1 && w != 2 && w != 4 && w != 8) w = 4;
  if (width) *width = w;

  int u = 0;
  if (ptr_arg && ptr_arg->kind == ND_ADDR && ptr_arg->lhs) {
    u = psx_node_is_unsigned_type(ptr_arg->lhs) ? 1 : 0;
  } else {
    u = psx_node_pointee_is_unsigned(ptr_arg) ? 1 : 0;
  }
  if (is_unsigned) *is_unsigned = u;
  return ptr_arg != NULL;
}

int psx_node_cast_i64_extension_info(node_t *node, int *target_size,
                                     int *widen_zext_i64, int *needs_i64_extend) {
  if (target_size) *target_size = 0;
  if (widen_zext_i64) *widen_zext_i64 = 0;
  if (needs_i64_extend) *needs_i64_extend = 0;
  if (!node) return 0;

  int sz = psx_type_sizeof(psx_node_get_type(node));
  if (sz <= 0) sz = ps_node_type_size(node);

  int zext = node->widen_zext_i64 ? 1 : 0;
  int extend = (!psx_node_value_is_pointer_like(node) && sz >= 8) ? 1 : 0;
  if (target_size) *target_size = sz;
  if (widen_zext_i64) *widen_zext_i64 = zext;
  if (needs_i64_extend) *needs_i64_extend = extend;
  return 1;
}

int psx_node_pointee_is_bool(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, NODE_POINTEE_BOOL);
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointee_is_bool(node->rhs);
    case ND_ADD:
    case ND_SUB:
      return psx_node_pointee_is_bool(node->lhs) ||
             psx_node_pointee_is_bool(node->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_is_bool(node->lhs);
    default:
      return pointee_flag_from_node_direct(node, NODE_POINTEE_BOOL);
  }
}

int psx_node_pointee_is_void(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, NODE_POINTEE_VOID);
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointee_is_void(node->rhs);
    case ND_ADD:
    case ND_SUB:
      return psx_node_pointee_is_void(node->lhs) ||
             psx_node_pointee_is_void(node->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_is_void(node->lhs);
    default:
      return pointee_flag_from_node_direct(node, NODE_POINTEE_VOID);
  }
}

int psx_node_pointee_is_const_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, NODE_POINTEE_CONST);
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointee_is_const_qualified(node->rhs);
    case ND_ADD:
    case ND_SUB:
      return psx_node_pointee_is_const_qualified(node->lhs) ||
             psx_node_pointee_is_const_qualified(node->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_is_const_qualified(node->lhs);
    default:
      return pointee_flag_from_node_direct(node, NODE_POINTEE_CONST);
  }
}

int psx_node_pointee_is_volatile_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, NODE_POINTEE_VOLATILE);
  if (node->type) return 0;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointee_is_volatile_qualified(node->rhs);
    case ND_ADD:
    case ND_SUB:
      return psx_node_pointee_is_volatile_qualified(node->lhs) ||
             psx_node_pointee_is_volatile_qualified(node->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_is_volatile_qualified(node->lhs);
    default:
      return pointee_flag_from_node_direct(node, NODE_POINTEE_VOLATILE);
  }
}

static int node_self_is_const_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type))
    return (psx_type_pointer_view_structural_qual_mask(type, 0) & 1u) ? 1 : 0;
  return type && type->is_const_qualified ? 1 : 0;
}

static int node_self_is_volatile_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type))
    return (psx_type_pointer_view_structural_qual_mask(type, 1) & 1u) ? 1 : 0;
  return type && type->is_volatile_qualified ? 1 : 0;
}

int psx_node_is_unsigned_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node->type) return scalar_flag_from_type(type, NODE_SCALAR_UNSIGNED);
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_is_unsigned_type(node->rhs);
    case ND_TERNARY: {
      psx_type_t *ternary_type = psx_node_get_type(node);
      if (ternary_type) return psx_type_is_unsigned(ternary_type);
      return 0;
    }
    default: {
      return scalar_flag_from_node_direct(node, NODE_SCALAR_UNSIGNED);
    }
  }
}

int psx_node_is_long_long_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node->type) return scalar_flag_from_type(type, NODE_SCALAR_LONG_LONG);
  switch (node->kind) {
    case ND_NUM:
      return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_LONG) ||
             ((node_num_t *)node)->int_is_long_long ? 1 : 0;
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_is_long_long_type(node->rhs);
    case ND_TERNARY:
      return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_LONG);
    default: {
      return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_LONG);
    }
  }
}

int psx_node_is_plain_char_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node->type) return scalar_flag_from_type(type, NODE_SCALAR_PLAIN_CHAR);
  if (node->kind == ND_NUM)
    return scalar_flag_from_node_direct(node, NODE_SCALAR_PLAIN_CHAR) ||
           ((node_num_t *)node)->int_is_plain_char ? 1 : 0;
  if (node->kind == ND_COMMA || node->kind == ND_STMT_EXPR)
    return psx_node_is_plain_char_type(node->rhs);
  if (node->kind == ND_TERNARY)
    return scalar_flag_from_node_direct(node, NODE_SCALAR_PLAIN_CHAR);
  return scalar_flag_from_node_direct(node, NODE_SCALAR_PLAIN_CHAR);
}

int psx_node_is_long_double_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node->type) return scalar_flag_from_type(type, NODE_SCALAR_LONG_DOUBLE);
  if (node->kind == ND_COMMA || node->kind == ND_STMT_EXPR)
    return psx_node_is_long_double_type(node->rhs);
  if (node->kind == ND_TERNARY)
    return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_DOUBLE);
  return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_DOUBLE);
}

tk_float_kind_t psx_node_pointee_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  int value = TK_FLOAT_KIND_NONE;
  if (pointer_view_from_node_direct(node, NODE_POINTER_POINTEE_FP_KIND, &value))
    return (tk_float_kind_t)value;
  if (node->type) return TK_FLOAT_KIND_NONE;
  switch (node->kind) {
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointee_fp_kind(node->rhs);
    /* ポインタ算術 (`a + 1`) / inc・dec (`a++`) の結果も同じ pointee を指す。
     * `*(a+1)` 等の deref が fp load になるよう pointee_fp_kind を継承する。 */
    case ND_ADD:
    case ND_SUB: {
      tk_float_kind_t l = psx_node_pointee_fp_kind(node->lhs);
      if (l != TK_FLOAT_KIND_NONE) return l;
      return psx_node_pointee_fp_kind(node->rhs);
    }
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_pointee_fp_kind(node->lhs);
    /* `double *g(); g()[i]` の subscript を fp load にするため、ポインタ戻り値の
     * pointee fp 種別を返す。 */
    default:
      return TK_FLOAT_KIND_NONE;
  }
}

/* pointer-to-VLA (`int (*p)[m]`) の行ストライドスロット (実行時値) のフレームオフセット。
 * 無ければ 0。ポインタ算術 (`p + 1`) のスケールに使う。ND_ADD/SUB は被演算子を辿る。 */
int psx_node_vla_row_stride_frame_off(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (vla_view_from_node_direct(node, NODE_VLA_ROW_STRIDE_FRAME_OFF, &value))
    return value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_LVAR: {
      vla_view_from_node_direct(node, NODE_VLA_ROW_STRIDE_FRAME_OFF, &value);
      return value;
    }
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_vla_row_stride_frame_off(node->lhs);
      if (l != 0) return l;
      return psx_node_vla_row_stride_frame_off(node->rhs);
    }
    default: {
      vla_view_from_node_direct(node, NODE_VLA_ROW_STRIDE_FRAME_OFF, &value);
      return value;
    }
  }
}

static int node_vla_strides_remaining(node_t *node) {
  if (!node) return 0;
  int value = 0;
  if (vla_view_from_node_direct(node, NODE_VLA_STRIDES_REMAINING, &value))
    return value;
  if (node->type) return 0;
  switch (node->kind) {
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_LVAR: {
      vla_view_from_node_direct(node, NODE_VLA_STRIDES_REMAINING, &value);
      return value;
    }
    case ND_ADD:
    case ND_SUB: {
      int l = node_vla_strides_remaining(node->lhs);
      if (l > 0) return l;
      return node_vla_strides_remaining(node->rhs);
    }
    default: {
      vla_view_from_node_direct(node, NODE_VLA_STRIDES_REMAINING, &value);
      return value;
    }
  }
}

static void node_pointer_stride_clear(int *inner_stride, int *next_stride,
                                      int *extra_strides, int *extra_strides_count) {
  if (inner_stride) *inner_stride = 0;
  if (next_stride) *next_stride = 0;
  if (extra_strides_count) *extra_strides_count = 0;
  if (extra_strides) {
    for (int i = 0; i < 5; i++) extra_strides[i] = 0;
  }
}

static int node_pointer_stride_from_type_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride, int *inner_stride,
    int *next_stride, int *extra_strides, int *extra_strides_count) {
  if (!psx_type_pointer_view_stride_sync_allowed_with_sidecar(
          type, sidecar_ptr_array_pointee_bytes, sidecar_outer_stride,
          sidecar_mid_stride)) {
    return 0;
  }
  return psx_type_pointer_view_effective_stride_metadata(
      type, inner_stride, next_stride, extra_strides, extra_strides_count);
}

static int node_pointer_stride_from_funcall_return(node_t *node, const psx_type_t *type,
                                                   int *inner_stride,
                                                   int *next_stride) {
  if (!node || node->kind != ND_FUNCALL || !type) return 0;
  psx_ret_pointee_array_t ret_array = type->funcptr_sig.function.callable.return_shape.pointee_array;
  if (!psx_ret_pointee_array_has_dims(ret_array)) return 0;
  int inner = 0;
  int next = 0;
  int row_size = ps_node_deref_size(node);
  psx_ret_pointee_array_t dims =
      psx_ret_pointee_array_make(ret_array.first_dim, ret_array.second_dim, 0);
  psx_ret_pointee_array_strides_from_row(dims, row_size, &inner, &next);
  if (inner <= 0 && next <= 0) return 0;
  if (inner_stride) *inner_stride = inner;
  if (next_stride) *next_stride = next;
  return 1;
}

static int node_pointer_stride_from_node_direct(node_t *node, int *inner_stride,
                                                int *next_stride, int *extra_strides,
                                                int *extra_strides_count) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node_type_state_stride(node, inner_stride, next_stride, extra_strides,
                             extra_strides_count)) {
    return 1;
  }
  if (node_pointer_stride_from_type_with_sidecar(
          type, 0, 0, 0,
          inner_stride, next_stride, extra_strides, extra_strides_count)) {
    return 1;
  }
  if (node_pointer_stride_from_funcall_return(node, type, inner_stride, next_stride))
    return 1;
  return 0;
}

int psx_node_pointer_stride_metadata(node_t *node, int *inner_stride,
                                     int *next_stride, int *extra_strides,
                                     int *extra_strides_count) {
  node_pointer_stride_clear(inner_stride, next_stride, extra_strides, extra_strides_count);
  if (!node) return 0;
  if (node_pointer_stride_from_node_direct(node, inner_stride, next_stride,
                                           extra_strides, extra_strides_count)) {
    return 1;
  }
  if (node->type) return 0;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
    case ND_ASSIGN:
    case ND_FUNCALL:
      return node_pointer_stride_from_node_direct(node, inner_stride, next_stride,
                                                  extra_strides, extra_strides_count);
    case ND_ADD:
    case ND_SUB:
      if (psx_node_pointer_stride_metadata(node->lhs, inner_stride, next_stride,
                                           extra_strides, extra_strides_count)) {
        return 1;
      }
      return psx_node_pointer_stride_metadata(node->rhs, inner_stride, next_stride,
                                              extra_strides, extra_strides_count);
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_pointer_stride_metadata(node->rhs, inner_stride, next_stride,
                                              extra_strides, extra_strides_count);
    default:
      return 0;
  }
}

void psx_node_get_tag_type(node_t *node, token_kind_t *tag_kind, char **tag_name, int *tag_len, int *is_tag_pointer) {
  node_tag_view_t view = node_tag_view_zero();
  if (node) {
    if (tag_view_from_node_direct(node, &view)) {
      if (tag_kind) *tag_kind = view.kind;
      if (tag_name) *tag_name = view.name;
      if (tag_len) *tag_len = view.len;
      if (is_tag_pointer) *is_tag_pointer = view.is_pointer;
      return;
    }
    if (node->type) {
      if (tag_kind) *tag_kind = view.kind;
      if (tag_name) *tag_name = view.name;
      if (tag_len) *tag_len = view.len;
      if (is_tag_pointer) *is_tag_pointer = view.is_pointer;
      return;
    }
    switch (node->kind) {
      case ND_LVAR:
      case ND_GVAR:
      case ND_DEREF:
      case ND_ADDR:
      case ND_STRING:
      case ND_CAST:
      case ND_FUNCALL:
        tag_view_from_node_direct(node, &view);
        break;
      case ND_ASSIGN:
        /* 代入式の結果は左辺の型。ノード自身に tag が無い (複合代入 `p += n` 等)
         * 場合は左辺から継承して `(p += n)->m` を解決できるようにする。 */
        tag_view_from_node_direct(node, &view);
        if (view.kind == TK_EOF) {
          psx_node_get_tag_type(node->lhs, &view.kind, &view.name, &view.len,
                                &view.is_pointer);
        }
        break;
      case ND_COMMA:
      case ND_STMT_EXPR:
        psx_node_get_tag_type(node->rhs, &view.kind, &view.name, &view.len,
                              &view.is_pointer);
        break;
      /* `p + n` のようなポインタ算術: tag info を pointer 側 (lhs) から継承する。
       * `(p+1)->x` や `(p+i).x` (`.` は通常 lvalue のみだが parser が許す形) で
       * tag が引けないと arrow/dot がエラーになる。 */
      case ND_ADD:
      case ND_SUB:
        psx_node_get_tag_type(node->lhs, &view.kind, &view.name, &view.len,
                              &view.is_pointer);
        if (view.kind == TK_EOF) {
          psx_node_get_tag_type(node->rhs, &view.kind, &view.name, &view.len,
                                &view.is_pointer);
        }
        break;
      /* `(cond ? a : b).x` 等の struct ternary 結果からメンバアクセスする際、
       * 両分岐は同型 struct のはずなので then 側から tag を引く。 */
      case ND_TERNARY: {
        node_ctrl_t *t = (node_ctrl_t *)node;
        psx_node_get_tag_type(t->base.rhs, &view.kind, &view.name, &view.len,
                              &view.is_pointer);
        if (view.kind == TK_EOF && t->els) {
          psx_node_get_tag_type(t->els, &view.kind, &view.name, &view.len,
                                &view.is_pointer);
        }
        break;
      }
      /* `(++p)->m` / `(p++)->m`: inc/dec はオペランドと同じ型なので tag を継承する。 */
      case ND_PRE_INC:
      case ND_PRE_DEC:
      case ND_POST_INC:
      case ND_POST_DEC:
        psx_node_get_tag_type(node->lhs, &view.kind, &view.name, &view.len,
                              &view.is_pointer);
        break;
      default:
        break;
    }
  }
  if (tag_kind) *tag_kind = view.kind;
  if (tag_name) *tag_name = view.name;
  if (tag_len) *tag_len = view.len;
  if (is_tag_pointer) *is_tag_pointer = view.is_pointer;
}

int psx_node_get_tag_scope_depth(node_t *node) {
  if (!node) return -1;
  node_tag_view_t view = node_tag_view_zero();
  if (tag_view_from_node_direct(node, &view))
    return view.scope_depth_p1 > 0 ? view.scope_depth_p1 - 1 : -1;
  if (node->type) return -1;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
    case ND_FUNCALL:
      tag_view_from_node_direct(node, &view);
      break;
    case ND_ASSIGN:
      tag_view_from_node_direct(node, &view);
      if (view.kind == TK_EOF && view.scope_depth_p1 <= 0)
        return psx_node_get_tag_scope_depth(node->lhs);
      break;
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_get_tag_scope_depth(node->rhs);
    case ND_ADD:
    case ND_SUB:
      {
        int d = psx_node_get_tag_scope_depth(node->lhs);
        if (d >= 0) return d;
      }
      return psx_node_get_tag_scope_depth(node->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return psx_node_get_tag_scope_depth(node->lhs);
    case ND_TERNARY: {
      node_ctrl_t *t = (node_ctrl_t *)node;
      int d = psx_node_get_tag_scope_depth(t->base.rhs);
      if (d < 0 && t->els) d = psx_node_get_tag_scope_depth(t->els);
      return d;
    }
    default:
      return -1;
  }
  return view.scope_depth_p1 > 0 ? view.scope_depth_p1 - 1 : -1;
}

static int node_is_unsigned(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_SHL || node->kind == ND_SHR) {
    /* Shift signedness doubles as the codegen ASR/LSR selector. Cast lowering
     * may override it independently of the lhs/result type. */
    return node->is_unsigned;
  }
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_FP_TO_INT:
      return scalar_flag_from_node_direct(node, NODE_SCALAR_UNSIGNED);
    case ND_CAST:
      return scalar_flag_from_node_direct(node, NODE_SCALAR_UNSIGNED) ||
             (node->is_unsigned ? 1 : 0);
    case ND_TERNARY: {
      psx_type_t *type = psx_node_get_type(node);
      return type ? type_result_unsigned(type) : node->is_unsigned;
    }
    case ND_FUNCALL: {
      psx_type_t *type = psx_node_get_type(node);
      return type ? type_result_unsigned(type) : node->is_unsigned;
    }
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR: {
      psx_type_t *type = psx_node_get_type(node);
      return type ? type_result_unsigned(type) : node->is_unsigned;
    }
    default: return node->is_unsigned;
  }
}

static int binary_usual_arith_unsigned(node_t *lhs, node_t *rhs) {
  return type_result_unsigned(type_from_operand_usual_arith(lhs, rhs));
}

int psx_node_integer_promotion_is_unsigned(node_t *node) {
  return type_uac_effective_unsigned(psx_node_get_type(node));
}

tk_float_kind_t psx_node_value_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  psx_type_t *type = psx_node_get_type(node);
  if (type && !psx_type_is_pointer(type) &&
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)) {
    return type->fp_kind != TK_FLOAT_KIND_NONE ? type->fp_kind : TK_FLOAT_KIND_DOUBLE;
  }
  if (node_has_explicit_type(node)) return TK_FLOAT_KIND_NONE;
  return (tk_float_kind_t)node->fp_kind;
}

int psx_node_value_is_complex(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && !psx_type_is_pointer(type)) return type->kind == PSX_TYPE_COMPLEX;
  if (node_has_explicit_type(node)) return 0;
  return node->is_complex ? 1 : 0;
}

int psx_node_value_is_void(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type) return type->kind == PSX_TYPE_VOID;
  if (node->type) return 0;
  return node->is_void_call ? 1 : 0;
}

int psx_node_integer_value_is_unsigned(node_t *node) {
  psx_type_t *type = psx_node_get_type(node);
  return type_is_integer_like(type) && psx_type_is_unsigned(type);
}

/* Conversion/codegen source signedness. This intentionally preserves legacy
 * operation overrides such as cast-lowered forced signed shifts. */
int psx_node_conversion_value_is_unsigned(node_t *node) {
  return node_is_unsigned(node);
}

/* Source signedness for widening an integer value to i64. */
int psx_node_i64_widen_source_is_unsigned(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (!type_is_integer_like(type)) return 0;
  int size = psx_type_sizeof(type);
  if (size <= 0) size = ps_node_type_size(node);
  return size >= 4 && node_is_unsigned(node);
}

/* Full shift operation signedness, including explicit cast-lowering overrides. */
int psx_node_shift_operation_is_unsigned(node_t *node) {
  if (!node || (node->kind != ND_SHL && node->kind != ND_SHR)) return 0;
  return node_is_unsigned(node);
}

int psx_node_usual_arith_operands_is_unsigned(node_t *lhs, node_t *rhs) {
  return binary_usual_arith_unsigned(lhs, rhs);
}

int psx_node_usual_arith_is_unsigned(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR:
    case ND_LT:
    case ND_LE:
    case ND_EQ:
    case ND_NE:
      return psx_node_usual_arith_operands_is_unsigned(node->lhs, node->rhs);
    case ND_TERNARY: {
      psx_type_t *type = psx_node_get_type(node);
      return type_result_unsigned(type);
    }
    default:
      return type_result_unsigned(psx_node_get_type(node));
  }
}

/* node の符号フラグを設定する (node_is_unsigned が読むフィールドに一致させる)。
 * `(int)u` / `(unsigned)i` キャストで結果の符号を確定するのに使う。 */
void psx_node_set_unsigned(node_t *node, int is_unsigned) {
  if (!node) return;
  node->is_unsigned = is_unsigned ? 1 : 0;
}

node_t *psx_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  node->fp_kind = psx_node_value_fp_kind(lhs);
  tk_float_kind_t rhs_fp = psx_node_value_fp_kind(rhs);
  if (rhs_fp > node->fp_kind) node->fp_kind = rhs_fp;

  if (kind == ND_EQ || kind == ND_NE || kind == ND_LT || kind == ND_LE ||
      kind == ND_LOGAND || kind == ND_LOGOR ||
      kind == ND_BITAND || kind == ND_BITXOR || kind == ND_BITOR ||
      kind == ND_SHL || kind == ND_SHR) {
    node->fp_kind = TK_FLOAT_KIND_NONE;
  }
  if (kind == ND_SHL || kind == ND_SHR) {
    int lhs_sz = ps_node_type_size(lhs);
    if (lhs_sz >= 4 && node_is_unsigned(lhs)) node->is_unsigned = 1;
    if (lhs_sz >= 8 && psx_node_is_long_long_type(lhs)) node->is_long_long = 1;
  } else if (kind == ND_ADD || kind == ND_SUB || kind == ND_MUL ||
             kind == ND_DIV || kind == ND_MOD || kind == ND_BITAND ||
             kind == ND_BITXOR || kind == ND_BITOR) {
    node->is_unsigned = binary_usual_arith_unsigned(lhs, rhs) ? 1 : 0;
    if (ps_node_type_size(node) >= 8 &&
        (psx_node_is_long_long_type(lhs) || psx_node_is_long_long_type(rhs))) {
      node->is_long_long = 1;
    }
  } else if (kind == ND_EQ || kind == ND_NE || kind == ND_LT || kind == ND_LE ||
             kind == ND_LOGAND || kind == ND_LOGOR) {
    node->is_unsigned = 0;
  } else if (node_is_unsigned(lhs) || node_is_unsigned(rhs)) {
    node->is_unsigned = 1;
  }
  // _Complex伝播: どちらかが_Complexなら結果も_Complex
  if (psx_node_value_is_complex(lhs) || psx_node_value_is_complex(rhs)) {
    node->is_complex = 1;
  }
  psx_type_t *type = type_from_binary_expr(node);
  if (type) {
    node->type = type;
    sync_plain_scalar_result_metadata_from_type(node, type);
  }
  return node;
}

node_t *psx_node_new_shift_trunc_extend(node_t *operand, int left_shift, int is_unsigned) {
  node_t *shl = psx_node_new_binary(ND_SHL, operand, psx_node_new_num(left_shift));
  node_t *shr = psx_node_new_binary(ND_SHR, shl, psx_node_new_num(left_shift));
  psx_node_set_unsigned(shl, is_unsigned ? 1 : 0);
  psx_node_set_unsigned(shr, is_unsigned ? 1 : 0);
  return shr;
}

node_t *psx_node_new_num(long long val) {
  node_num_t *node = arena_alloc(sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->val = val;
  return (node_t *)node;
}

static node_lvar_t *new_lvar_symbol_node(int offset, lvar_t *var,
                                         psx_type_t *type) {
  node_lvar_t *node = arena_alloc(sizeof(node_lvar_t));
  node->base.kind = ND_LVAR;
  node->base.type = type;
  node->offset = offset;
  node->var = var;
  return node;
}

node_t *psx_node_new_lvar(int offset) {
  return (node_t *)new_lvar_symbol_node(
      offset, NULL, psx_type_new_integer(TK_INT, 8, 0));
}

node_t *psx_node_new_lvar_typed(int offset, int type_size) {
  int size = type_size > 0 ? type_size : 8;
  return (node_t *)new_lvar_symbol_node(
      offset, NULL, psx_type_new_integer(TK_INT, size, 0));
}

node_t *psx_node_new_lvar_typed_at_for(lvar_t *owner, int offset, int type_size) {
  psx_type_t *type = NULL;
  if (owner) {
    psx_type_t *owner_type = psx_lvar_get_decl_type(owner);
    int rel = offset - owner->offset;
    if (rel == 0 && psx_type_sizeof(owner_type) == type_size)
      type = owner_type;
    else if (rel >= 0 && owner->elem_size > 0 &&
             (rel % owner->elem_size) == 0)
      type = type_array_element_type_for_size(owner_type, type_size);
  }
  if (!type) type = psx_type_new_integer(TK_INT, type_size > 0 ? type_size : 8, 0);
  return (node_t *)new_lvar_symbol_node(offset, owner, type);
}

node_t *psx_node_new_lvar_scalar_slot_at(int offset, int type_size,
                                         tk_float_kind_t fp_kind, int is_bool) {
  psx_type_t *type = fp_kind != TK_FLOAT_KIND_NONE
                         ? psx_type_new_float(fp_kind, type_size)
                     : is_bool
                         ? psx_type_new_integer(TK_BOOL, type_size, 1)
                         : psx_type_new_integer(TK_INT, type_size, 0);
  return (node_t *)new_lvar_symbol_node(offset, NULL, type);
}

node_t *psx_node_new_lvar_fp_slot_at(int offset, int type_size, tk_float_kind_t fp_kind) {
  return psx_node_new_lvar_scalar_slot_at(offset, type_size, fp_kind, 0);
}

node_t *psx_node_new_lvar_fp_slot_for(lvar_t *owner, int offset, int type_size) {
  tk_float_kind_t fp_kind = psx_lvar_fp_kind(owner);
  psx_type_t *type = fp_kind != TK_FLOAT_KIND_NONE
                         ? psx_type_new_float(fp_kind, type_size)
                         : psx_type_new_integer(TK_INT, type_size, 0);
  return (node_t *)new_lvar_symbol_node(offset, owner, type);
}

node_t *psx_node_new_param_placeholder(int is_pointer, tk_float_kind_t fp_kind,
                                       int is_unsigned) {
  if (is_pointer) {
    int fp_size = fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8;
    psx_type_t *base = fp_kind != TK_FLOAT_KIND_NONE
                           ? psx_type_new_float(fp_kind, fp_size)
                           : psx_type_new_integer(
                                 is_unsigned ? TK_UNSIGNED : TK_INT, 4,
                                 is_unsigned);
    return (node_t *)new_lvar_symbol_node(
        0, NULL, psx_type_new_pointer(base, psx_type_sizeof(base)));
  }
  node_t *node = psx_node_new_num(0);
  node->fp_kind = fp_kind;
  node->is_unsigned = is_unsigned ? 1 : 0;
  return node;
}

node_t *psx_node_new_unsigned_lvar_typed(int offset, int type_size) {
  return (node_t *)new_lvar_symbol_node(
      offset, NULL, psx_type_new_integer(TK_UNSIGNED, type_size, 1));
}

node_t *psx_node_new_lvar_for(lvar_t *var) {
  psx_type_t *type = var ? psx_lvar_get_decl_type(var) : NULL;
  if (!type) type = psx_type_new_integer(TK_INT, 8, 0);
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, type);
}

node_t *psx_node_new_lvar_typed_for(lvar_t *var, int type_size) {
  psx_type_t *type = var ? psx_lvar_get_decl_type(var) : NULL;
  if (!type)
    type = psx_type_new_integer(TK_INT, type_size > 0 ? type_size : 8, 0);
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, type);
}

static int lvar_public_storage_size_or_elem(const lvar_t *var) {
  int elem_size = psx_lvar_elem_size(var, 0);
  return psx_lvar_storage_size(var, elem_size);
}

node_t *psx_node_new_lvar_object_ref_for(lvar_t *var) {
  return psx_node_new_lvar_typed_for(var, lvar_public_storage_size_or_elem(var));
}

node_t *psx_node_new_lvar_expr_ref_for(lvar_t *var, int is_pointer) {
  psx_type_t *decl_type = var ? psx_lvar_get_decl_type(var) : NULL;
  if (!decl_type) {
    psx_type_t *base = psx_type_new_integer(TK_INT,
                                            var && var->elem_size > 0
                                                ? var->elem_size : 4, 0);
    decl_type = is_pointer ? psx_type_new_pointer(base, psx_type_sizeof(base))
                           : base;
  }
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, decl_type);
}

node_t *psx_node_new_lvar_identifier_ref_for(lvar_t *var) {
  if (var && var->is_static_local && var->static_global_name) {
    int sz = var->size > 0 ? var->size : var->elem_size;
    return psx_node_new_static_local_gvar_for(var, sz);
  }

  return psx_node_new_lvar_for(var);
}

node_t *psx_node_new_param_lvar_for(lvar_t *var, int abi_type_size,
                                    int is_unsigned, tk_float_kind_t abi_fp_kind,
                                    int is_complex) {
  psx_type_t *decl_type = var ? psx_lvar_get_decl_type(var) : NULL;
  if (!decl_type) {
    if (is_complex) {
      decl_type = psx_type_new(PSX_TYPE_COMPLEX);
      decl_type->size = abi_type_size;
      decl_type->fp_kind = abi_fp_kind;
    } else if (abi_fp_kind != TK_FLOAT_KIND_NONE) {
      decl_type = psx_type_new_float(abi_fp_kind, abi_type_size);
    } else {
      decl_type = psx_type_new_integer(
          is_unsigned ? TK_UNSIGNED : TK_INT, abi_type_size, is_unsigned);
    }
  }
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, decl_type);
}

node_t *psx_node_new_array_elem_lvar_for(lvar_t *var, int idx) {
  psx_type_t *elem_type =
      var ? type_array_leaf_element_type(psx_lvar_get_decl_type(var)) : NULL;
  int canonical_elem_size = psx_type_sizeof(elem_type);
  int elem_size = canonical_elem_size > 0 ? canonical_elem_size
                  : (var ? var->elem_size : 0);
  int offset = var ? var->offset + idx * elem_size : 0;
  if (!elem_type)
    elem_type = psx_type_new_integer(TK_INT, elem_size > 0 ? elem_size : 4,
                                     var ? var->is_unsigned : 0);
  return (node_t *)new_lvar_symbol_node(offset, var, elem_type);
}

static node_t *annotate_explicit_type(node_t *node, psx_type_t *type) {
  if (node && type) node->type = type;
  return node;
}

node_t *psx_node_new_fp_to_int_cast(node_t *operand, int width, psx_type_t *cast_type) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_FP_TO_INT;
  node->lhs = operand;
  node->fp_kind = TK_FLOAT_KIND_NONE;
  if (!cast_type) {
    int type_size = width == 8 ? 8 : 4;
    cast_type = psx_type_new_integer(TK_INT, type_size, 0);
  }
  return annotate_explicit_type(node, cast_type);
}

node_t *psx_node_new_int_to_fp_cast(node_t *operand, tk_float_kind_t target,
                                    psx_type_t *cast_type) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_INT_TO_FP;
  node->lhs = operand;
  node->fp_kind = target;
  if (!cast_type) {
    cast_type = psx_type_new_float(target, target == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  return annotate_explicit_type(node, cast_type);
}

node_t *psx_node_new_integer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         int type_size, int is_unsigned,
                                         int is_long_long) {
  return psx_node_new_integer_cast_result_ex(operand, cast_type, type_size, is_unsigned,
                                             is_long_long, 0, 0);
}

node_t *psx_node_new_integer_cast_result_ex(node_t *operand, psx_type_t *cast_type,
                                            int type_size, int is_unsigned,
                                            int is_long_long, int is_plain_char,
                                            int widen_zext_i64) {
  node_t *wrap = arena_alloc(sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  wrap->widen_zext_i64 = widen_zext_i64 ? 1 : 0;
  if (!cast_type) {
    token_kind_t scalar_kind = is_plain_char
                                   ? TK_CHAR
                                   : (is_unsigned ? TK_UNSIGNED : TK_INT);
    cast_type = psx_type_new_integer(scalar_kind, type_size, is_unsigned);
    cast_type->is_long_long = is_long_long ? 1 : 0;
  }
  return annotate_explicit_type(wrap, cast_type);
}

node_t *psx_node_new_i64_to_i32_trunc_cast(node_t *operand, psx_type_t *cast_type,
                                           int is_unsigned) {
  node_t *trunc = psx_node_new_shift_trunc_extend(operand, 32, is_unsigned);
  return psx_node_new_integer_cast_result(trunc, cast_type, 4, is_unsigned, 0);
}

node_t *psx_node_new_pointer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         token_kind_t type_kind,
                                         token_kind_t tag_kind,
                                         char *tag_name, int tag_len,
                                         int elem_size, int is_unsigned) {
  node_t *wrap = arena_alloc(sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  int pointer_levels = 1;
  if (!cast_type) {
    psx_type_t *base = NULL;
    if (type_kind == TK_VOID) {
      base = type_new_void();
    } else if (psx_ctx_is_tag_aggregate_kind(tag_kind)) {
      int tag_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      if (tag_size <= 0) tag_size = elem_size;
      base = psx_type_new_tag(tag_kind, tag_name, tag_len, 0, tag_size);
    } else if (type_kind == TK_FLOAT || type_kind == TK_DOUBLE) {
      tk_float_kind_t fp_kind = type_kind == TK_FLOAT
                                    ? TK_FLOAT_KIND_FLOAT
                                    : TK_FLOAT_KIND_DOUBLE;
      base = psx_type_new_float(fp_kind, elem_size > 0 ? elem_size : 8);
    } else if (type_kind == TK_BOOL) {
      base = psx_type_new_integer(TK_BOOL, elem_size > 0 ? elem_size : 1, 1);
    } else {
      base = psx_type_new_integer(
          is_unsigned ? TK_UNSIGNED : TK_INT,
          elem_size > 0 ? elem_size : 4, is_unsigned);
    }
    int deref_size = elem_size > 0 ? elem_size : 8;
    int base_deref_size = elem_size > 0 ? elem_size : 8;
    cast_type = psx_type_wrap_pointer_levels(
        base, pointer_levels, deref_size, base_deref_size, 0, 0);
  }
  return annotate_explicit_type(wrap, cast_type);
}

node_t *psx_node_new_aggregate_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_t *wrap = arena_alloc(sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  return annotate_explicit_type(wrap, cast_type);
}

node_t *psx_node_new_void_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_t *wrap = arena_alloc(sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  if (!cast_type) cast_type = type_new_void();
  return annotate_explicit_type(wrap, cast_type);
}

static node_t *new_addr_node(node_t *base) {
  node_t *addr = arena_alloc(sizeof(node_t));
  addr->kind = ND_ADDR;
  addr->lhs = base;
  return addr;
}

static void init_array_addr_canonical_type(node_t *addr,
                                           psx_type_t *array_type) {
  if (!addr || !array_type) return;
  addr->type = array_type->kind == PSX_TYPE_ARRAY
                   ? type_decay_array_to_pointer(array_type)
                   : (type_is_pointer_view_type(array_type) ? array_type : NULL);
}

static psx_type_t *type_from_address_operand(node_t *operand) {
  psx_type_t *base = psx_node_get_type(operand);
  if (!base) return NULL;
  int deref_size = psx_type_sizeof(base);
  if (deref_size <= 0) deref_size = ps_node_type_size(operand);
  if (deref_size <= 0) deref_size = 8;
  psx_type_t *type = psx_type_new_pointer(base, deref_size);
  int operand_levels = psx_node_pointer_qual_levels(operand);
  type->pointer_qual_levels = operand_levels > 0 ? operand_levels + 1 : 1;
  int operand_base_deref_size = psx_node_base_deref_size(operand);
  type->base_deref_size = operand_base_deref_size > 0 ? operand_base_deref_size
                                                      : deref_size;
  type->pointer_const_qual_mask = psx_node_pointer_const_qual_mask(operand) << 1;
  type->pointer_volatile_qual_mask =
      psx_node_pointer_volatile_qual_mask(operand) << 1;
  return type;
}

static psx_type_t *type_decay_array_to_pointer(psx_type_t *array_type) {
  if (!array_type || array_type->kind != PSX_TYPE_ARRAY || !array_type->base)
    return NULL;
  int elem_size = psx_type_sizeof(array_type->base);
  if (elem_size <= 0) elem_size = psx_type_deref_size(array_type);
  if (elem_size <= 0) elem_size = array_type->elem_size;
  if (elem_size <= 0) elem_size = 8;
  psx_type_t *ptr = psx_type_new_pointer(array_type->base, elem_size);
  if (array_type->base->kind == PSX_TYPE_POINTER) {
    int base_levels = array_type->base->pointer_qual_levels > 0
                          ? array_type->base->pointer_qual_levels
                          : type_pointer_depth(array_type->base);
    ptr->pointer_qual_levels = base_levels + 1;
  }
  ptr->base_deref_size = array_type->base_deref_size > 0
                             ? array_type->base_deref_size
                             : psx_type_deref_size(array_type->base);
  if (ptr->base_deref_size <= 0) ptr->base_deref_size = elem_size;
  ptr->pointee_fp_kind = array_type->pointee_fp_kind;
  ptr->funcptr_sig = psx_decl_funcptr_sig_clone(array_type->funcptr_sig);
  int ptr_array_pointee_bytes =
      psx_type_pointer_view_structural_ptr_array_pointee_bytes(ptr);
  if (ptr_array_pointee_bytes > 0)
    ptr->ptr_array_pointee_bytes = ptr_array_pointee_bytes;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (psx_type_pointer_view_stride_metadata(ptr, &inner_stride, &next_stride,
                                            extra_strides, &extra_count)) {
    ptr->outer_stride = inner_stride;
    ptr->mid_stride = next_stride;
    ptr->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      ptr->extra_strides[i] = extra_strides[i];
  }
  ptr->vla_row_stride_frame_off =
      psx_type_pointer_view_vla_row_stride_frame_off(array_type);
  ptr->vla_strides_remaining =
      psx_type_pointer_view_vla_strides_remaining(array_type);
  return ptr;
}

static psx_decl_funcptr_sig_t funcptr_sig_for_deref_result(psx_decl_funcptr_sig_t sig,
                                                           const psx_type_t *base,
                                                           int pointer_levels) {
  if (!base || pointer_levels > 1 || !sig.function.callable.return_shape.is_data_pointer ||
      psx_ret_pointee_array_has_dims(sig.function.callable.return_shape.pointee_array)) {
    return sig;
  }

  sig.function.callable.return_shape.is_data_pointer = 0;
  sig.function.callable.return_shape.is_void = 0;
  sig.function.callable.return_shape.is_complex = 0;
  sig.function.callable.return_shape.int_width = 0;
  sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_NONE;
  sig.function.callable.return_shape.pointee_fp_kind = TK_FLOAT_KIND_NONE;

  switch (base->kind) {
    case PSX_TYPE_VOID:
      sig.function.callable.return_shape.is_void = 1;
      break;
    case PSX_TYPE_COMPLEX:
      sig.function.callable.return_shape.is_complex = 1;
      sig.function.callable.return_shape.fp_kind =
          base->fp_kind != TK_FLOAT_KIND_NONE ? base->fp_kind : TK_FLOAT_KIND_DOUBLE;
      break;
    case PSX_TYPE_FLOAT:
      sig.function.callable.return_shape.fp_kind = base->fp_kind;
      break;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER: {
      int width = psx_type_sizeof(base);
      sig.function.callable.return_shape.int_width = (unsigned char)(width >= 8 ? 8 : 4);
      break;
    }
    default:
      break;
  }
  return sig;
}

static psx_type_t *type_from_deref_operand(node_t *operand) {
  psx_type_t *type = psx_node_get_type(operand);
  if (!type_is_pointer_view_type(type) || !type->base) return NULL;
  if (!operand || !operand->type)
    psx_type_canonicalize_flat_pointer_to_array(type);
  int pointer_levels = psx_type_pointer_view_structural_qual_levels(type);
  int ptr_array_pointee_bytes =
      psx_type_pointer_view_structural_ptr_array_pointee_bytes(type);
  int has_structural_stride = psx_type_pointer_view_stride_metadata(
      type, NULL, NULL, NULL, NULL);
  if (type->kind == PSX_TYPE_ARRAY && type->pointer_qual_levels <= 0) {
    return type->base;
  }
  if (type->kind == PSX_TYPE_POINTER && type->base->kind == PSX_TYPE_ARRAY &&
      pointer_levels <= 1) {
    return type_array_with_pointer_element_storage(type->base);
  }
  if ((!type_is_pointer_view_type(type->base) || ptr_array_pointee_bytes > 0) &&
      pointer_levels <= 1) {
    int elem_size = type->base_deref_size > 0 ? type->base_deref_size
                                              : psx_type_sizeof(type->base);
    if (ptr_array_pointee_bytes > 0 && type_is_pointer_view_type(type->base)) {
      int pointer_elem_size = psx_type_sizeof(type->base);
      if (pointer_elem_size > 0) elem_size = pointer_elem_size;
    }
    int is_array_view = (type->kind == PSX_TYPE_ARRAY &&
                        type->pointer_qual_levels == 0) ||
                        ptr_array_pointee_bytes > 0 ||
                        has_structural_stride ||
                        psx_type_pointer_view_raw_array_shape_has_hint(type);
    int array_size = type->deref_size;
    if (has_structural_stride && type->kind == PSX_TYPE_POINTER &&
        type->base && type->base->kind == PSX_TYPE_ARRAY) {
      int base_size = psx_type_sizeof(type->base);
      if (base_size > array_size) array_size = base_size;
    } else {
      int raw_array_size_hint =
          psx_type_pointer_view_raw_array_size_hint(type);
      if (raw_array_size_hint > array_size) array_size = raw_array_size_hint;
    }
    if (ptr_array_pointee_bytes > array_size)
      array_size = ptr_array_pointee_bytes;
    if (is_array_view && elem_size > 0 && array_size >= elem_size) {
      int array_len = array_size / elem_size;
      if (array_len <= 0) array_len = 1;
      psx_type_t *array =
          psx_type_new_array(type->base, array_len, array_size, elem_size, 0);
      if (type_is_pointer_view_type(type->base)) {
        array->pointer_qual_levels =
            type->base->pointer_qual_levels > 0 ? type->base->pointer_qual_levels : 1;
        array->base_deref_size =
            type->base->base_deref_size > 0 ? type->base->base_deref_size
                                            : psx_type_deref_size(type->base);
      } else {
        array->base_deref_size = elem_size;
      }
      array->pointee_fp_kind = type->pointee_fp_kind;
      array->ptr_array_pointee_bytes =
          (type_is_pointer_to_array_type(type->base) ||
           psx_type_is_tag_aggregate(type->base))
              ? ptr_array_pointee_bytes : 0;
      psx_type_copy_pointer_view_stride_metadata(array, type);
      return type_array_with_pointer_element_storage(array);
    }
  }
  if (!type_is_pointer_view_type(type->base) && pointer_levels >= 2) {
    int deref_size = type->base_deref_size > 0 ? type->base_deref_size
                                               : psx_type_sizeof(type->base);
    if (deref_size <= 0) deref_size = type->deref_size;
    if (deref_size <= 0) deref_size = 8;
    psx_type_t *result = psx_type_new_pointer(type->base, deref_size);
    result->pointer_qual_levels = pointer_levels - 1;
    result->base_deref_size = deref_size;
    result->pointer_const_qual_mask =
        psx_type_pointer_view_structural_qual_mask(type, 0) >> 1;
    result->pointer_volatile_qual_mask =
        psx_type_pointer_view_structural_qual_mask(type, 1) >> 1;
    result->pointee_fp_kind = type->pointee_fp_kind;
    result->funcptr_sig =
        psx_decl_funcptr_sig_clone(funcptr_sig_for_deref_result(
            type->funcptr_sig, type->base, result->pointer_qual_levels));
    result->ptr_array_pointee_bytes =
        psx_type_pointer_view_structural_ptr_array_pointee_bytes(type);
    psx_type_copy_pointer_view_stride_metadata(result, type);
    return result;
  }
  return type_array_with_pointer_element_storage(type->base);
}

static psx_type_t *type_normalize_tag_aggregate_size(psx_type_t *type,
                                                     int value_size) {
  if (!psx_type_is_tag_aggregate(type) || value_size <= 0 ||
      value_size >= psx_type_sizeof(type)) {
    return type;
  }
  psx_type_t *tag = psx_type_new_tag(type->tag_kind, type->tag_name,
                                     type->tag_len,
                                     type->tag_scope_depth_p1,
                                     value_size);
  psx_type_copy_common_qualifiers(tag, type);
  tag->funcptr_sig = psx_decl_funcptr_sig_clone(type->funcptr_sig);
  return tag;
}

static psx_type_t *type_from_subscript_base_type(const psx_type_t *base_type,
                                                 int elem_size,
                                                 int inner_deref_size,
                                                 int next_deref_size,
                                                 const int *extra_strides,
                                                 int extra_strides_count) {
  if (!base_type) return NULL;
  if (base_type->kind == PSX_TYPE_POINTER && base_type->base &&
      base_type->base->kind == PSX_TYPE_POINTER &&
      base_type->base->base && base_type->base->base->kind == PSX_TYPE_ARRAY) {
    int pointer_elem_size = psx_type_sizeof(base_type->base);
    if (pointer_elem_size > 0 && elem_size == pointer_elem_size) {
      return type_with_funcptr_sig(base_type->base, base_type->funcptr_sig);
    }
  }
  if (base_type->kind == PSX_TYPE_POINTER && base_type->base &&
      base_type->base->kind == PSX_TYPE_ARRAY &&
      (!base_type->base->base || base_type->base->base->kind != PSX_TYPE_ARRAY) &&
      psx_type_deref_size(base_type->base) <= psx_type_sizeof(base_type->base->base)) {
    (void)elem_size;
    (void)next_deref_size;
    (void)extra_strides;
    (void)extra_strides_count;
    return base_type->base;
  }
  const psx_type_t *view = base_type;
  if (view->kind == PSX_TYPE_POINTER && view->base) view = view->base;
  if (!view || view->kind != PSX_TYPE_ARRAY || !view->base) {
    return NULL;
  }

  int base_elem_size = psx_type_sizeof(view->base);
  int subscript_yields_pointer_element =
      view->base->kind == PSX_TYPE_POINTER &&
      base_elem_size > 0 &&
      ((inner_deref_size > 0 && inner_deref_size == base_elem_size) ||
       (inner_deref_size <= 0 && elem_size == base_elem_size));
  int subscript_yields_tag_element =
      psx_type_is_tag_aggregate(view->base) &&
      base_elem_size > 0 && elem_size == base_elem_size;
  int keeps_row = inner_deref_size > 0 && elem_size > inner_deref_size &&
                  !subscript_yields_pointer_element &&
                  !subscript_yields_tag_element;
  if (!keeps_row) {
    psx_type_t *elem_type = type_with_funcptr_sig(view->base,
                                                  view->funcptr_sig);
    return type_normalize_tag_aggregate_size(elem_type, elem_size);
  }

  int row_elem_size = inner_deref_size > 0 ? inner_deref_size
                                           : psx_type_sizeof(view->base);
  if (row_elem_size <= 0) row_elem_size = view->elem_size;
  if (row_elem_size <= 0) return view->base;
  if (view->base->kind == PSX_TYPE_ARRAY &&
      (psx_type_sizeof(view->base) == elem_size ||
       (view->ptr_array_pointee_bytes > 0 &&
        psx_type_sizeof(view->base) == row_elem_size))) {
    return view->base;
  }
  int row_len = elem_size / row_elem_size;
  if (row_len <= 0) row_len = 1;
  psx_type_t *row_base = view->base;
  int leaf_size = psx_type_sizeof(view->base);
  if (view->base && view->base->kind != PSX_TYPE_ARRAY &&
      leaf_size > 0 && row_elem_size > leaf_size &&
      (row_elem_size % leaf_size) == 0) {
    int inner_len = row_elem_size / leaf_size;
    row_base = psx_type_new_array(view->base, inner_len, row_elem_size,
                                  leaf_size, view->is_vla);
    row_base->base_deref_size = view->base_deref_size > 0
                                    ? view->base_deref_size
                                    : leaf_size;
  }
  psx_type_t *row = psx_type_new_array(row_base, row_len, elem_size,
                                       row_elem_size, view->is_vla);
  row->base_deref_size = view->base_deref_size > 0
                             ? view->base_deref_size
                             : row_elem_size;
  row->outer_stride = next_deref_size;
  row->pointee_fp_kind = view->pointee_fp_kind;
  row->funcptr_sig = psx_decl_funcptr_sig_clone(view->funcptr_sig);
  row->ptr_array_pointee_bytes = view->ptr_array_pointee_bytes;
  row->vla_row_stride_frame_off =
      psx_type_pointer_view_vla_row_stride_frame_off(view);
  int view_vla_strides_remaining =
      psx_type_pointer_view_vla_strides_remaining(view);
  row->vla_strides_remaining = view_vla_strides_remaining > 0
                                   ? view_vla_strides_remaining - 1
                                   : view_vla_strides_remaining;
  int n = extra_strides_count;
  if (n < 0) n = 0;
  if (n > 5) n = 5;
  if (n > 0) {
    row->mid_stride = extra_strides[0];
    int shifted = n - 1;
    row->extra_strides_count = (unsigned char)shifted;
    for (int i = 0; i < shifted; i++) row->extra_strides[i] = extra_strides[i + 1];
    for (int i = shifted; i < 5; i++) row->extra_strides[i] = 0;
  } else {
    row->mid_stride = 0;
    row->extra_strides_count = 0;
    for (int i = 0; i < 5; i++) row->extra_strides[i] = 0;
  }
  return row;
}

node_t *psx_node_new_gvar_array_addr_for(global_var_t *gv) {
  node_t *addr = new_addr_node(psx_node_new_gvar_array_base_for(gv));
  init_array_addr_canonical_type(addr, psx_gvar_get_decl_type(gv));
  return addr;
}

node_t *psx_node_new_static_local_array_addr_for(lvar_t *var, int gvar_type_size) {
  node_t *addr = new_addr_node(
      psx_node_new_static_local_gvar_for(var, gvar_type_size));
  init_array_addr_canonical_type(addr, static_local_backing_decl_type(var));
  return addr;
}

node_t *psx_node_new_lvar_array_addr_for(lvar_t *var, int is_tag_pointer) {
  (void)is_tag_pointer;
  node_t *addr = new_addr_node(psx_node_new_lvar_for(var));
  init_array_addr_canonical_type(addr, psx_lvar_get_decl_type(var));
  return addr;
}

node_t *psx_node_new_compound_gvar_array_addr_for(global_var_t *gv,
                                                  int ptr_array_pointee_bytes,
                                                  int pointer_elem_size,
                                                  int array_size,
                                                  psx_type_t *canonical_type) {
  (void)ptr_array_pointee_bytes;
  (void)pointer_elem_size;
  node_t *addr = new_addr_node(psx_node_new_gvar_for(gv));
  init_array_addr_canonical_type(addr, canonical_type ? canonical_type
                                                      : psx_gvar_get_decl_type(gv));
  addr->type_state.compound_literal_array_size = array_size;
  return addr;
}

node_t *psx_node_new_compound_lvar_array_addr_for(lvar_t *var,
                                                  token_kind_t tag_kind,
                                                  char *tag_name, int tag_len,
                                                  int array_size,
                                                  psx_type_t *canonical_type) {
  (void)tag_kind;
  (void)tag_name;
  (void)tag_len;
  node_t *addr = new_addr_node(psx_node_new_lvar_for(var));
  init_array_addr_canonical_type(addr, canonical_type ? canonical_type
                                                      : psx_lvar_get_decl_type(var));
  addr->type_state.compound_literal_array_size = array_size;
  return addr;
}

node_t *psx_node_new_addr_value_for(node_t *operand) {
  node_t *addr = new_addr_node(operand);
  addr->type = type_from_address_operand(operand);
  return addr;
}

node_t *psx_node_new_explicit_addr_value_for(node_t *operand) {
  if (!operand || operand->kind != ND_ADDR) return operand;
  node_t *cp = arena_alloc(sizeof(node_t));
  *cp = *operand;
  cp->type = type_from_address_operand(operand->lhs);
  cp->type_state = (psx_expr_type_state_t){0};
  cp->is_explicit_addr_expr = 1;
  return cp;
}

node_t *psx_node_new_unary_addr_for(node_t *operand) {
  node_t *node = new_addr_node(operand);
  node->type = type_from_address_operand(operand);
  node->is_explicit_addr_expr = 1;
  return node;
}

static void init_unary_deref_expr_state(node_t *result, node_t *operand) {
  int deref_size = ps_node_deref_size(operand);
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (deref_size <= 0 ||
      !psx_node_pointer_stride_metadata(
          operand, &inner_stride, &next_stride, extra_strides, &extra_count) ||
      inner_stride <= 0 || deref_size <= inner_stride) {
    return;
  }

  int result_inner = next_stride;
  int result_next = extra_count > 0 ? extra_strides[0] : 0;
  int result_extra[5] = {0};
  int result_extra_count = extra_count > 0 ? extra_count - 1 : 0;
  for (int i = 0; i < result_extra_count; i++)
    result_extra[i] = extra_strides[i + 1];
  node_type_state_store_stride(result, result_inner, result_next,
                               result_extra, result_extra_count);
}

static void init_subscript_expr_state(node_t *result, int next_stride,
                                      const int *extra_strides,
                                      int extra_count) {
  if (!result || !result->type || result->type->kind != PSX_TYPE_ARRAY) return;
  result->type_state.subscript_uses_base_address = 1;
  int result_next = extra_count > 0 && extra_strides ? extra_strides[0] : 0;
  int result_extra[5] = {0};
  int result_extra_count = extra_count > 0 ? extra_count - 1 : 0;
  if (result_extra_count > 5) result_extra_count = 5;
  for (int i = 0; i < result_extra_count; i++)
    result_extra[i] = extra_strides[i + 1];
  node_type_state_store_stride(result, next_stride, result_next,
                               result_extra, result_extra_count);
}

static psx_type_t *type_from_vla_subscript_result(
    psx_type_t *base_type, psx_type_t *subscript_type,
    int elem_size, int inner_deref_size,
    int parent_row_offset, int parent_remaining) {
  int result_row_offset =
      parent_remaining > 0 ? parent_row_offset + 8 : 0;
  int result_remaining = parent_remaining > 0 ? parent_remaining - 1 : 0;
  int keeps_row = inner_deref_size > 0;
  if (keeps_row && (!subscript_type ||
                    subscript_type->kind != PSX_TYPE_ARRAY)) {
    int row_size = elem_size;
    if (row_size <= inner_deref_size) row_size = inner_deref_size * 2;
    return psx_type_new_runtime_vla_row_view(
        base_type, row_size, inner_deref_size,
        result_row_offset, result_remaining);
  }
  if (!subscript_type) return NULL;
  psx_type_t *result = arena_alloc(sizeof(psx_type_t));
  *result = *subscript_type;
  result->vla_row_stride_frame_off = result_row_offset;
  result->vla_strides_remaining = result_remaining;
  return result;
}

node_t *psx_node_new_tag_member_deref_for(node_t *addr_base, node_t *base,
                                          const tag_member_info_t *info) {
  if (!info) return NULL;
  node_t *addr = psx_node_new_binary(ND_ADD, addr_base, psx_node_new_num(info->offset));
  node_t *deref = arena_alloc(sizeof(node_t));
  deref->kind = ND_DEREF;
  deref->lhs = addr;
  int mem_size = psx_tag_member_decl_value_size(info);
  int mem_array_len = psx_tag_member_decl_array_count(info);
  int mem_is_ptr = psx_tag_member_decl_is_pointer(info);
  tk_float_kind_t mem_fp_kind = psx_tag_member_decl_fp_kind(info);
  int mem_is_bool = psx_tag_member_decl_is_bool(info);
  int mem_is_unsigned = psx_tag_member_decl_is_unsigned(info);
  int member_is_const =
      psx_node_pointee_is_const_qualified(base) ||
      (!ps_node_is_pointer(base) && node_self_is_const_qualified(base));
  int member_is_volatile =
      psx_node_pointee_is_volatile_qualified(base) ||
      (!ps_node_is_pointer(base) && node_self_is_volatile_qualified(base));
  deref->type_state.bit_width = (unsigned char)info->bit_width;
  deref->type_state.bit_offset = (unsigned char)info->bit_offset;
  deref->type_state.bit_is_signed = info->bit_is_signed ? 1 : 0;
  psx_decl_funcptr_sig_t member_funcptr_sig = psx_ctx_tag_member_funcptr_sig(info);
  if (psx_decl_funcptr_sig_has_payload(member_funcptr_sig) &&
      member_funcptr_sig.function.callable.return_shape.fp_kind == TK_FLOAT_KIND_NONE &&
      !member_funcptr_sig.function.callable.return_shape.is_data_pointer &&
      mem_fp_kind != TK_FLOAT_KIND_NONE) {
    member_funcptr_sig.function.callable.return_shape.fp_kind = mem_fp_kind;
  }
  psx_type_t *decl_type = (psx_type_t *)psx_tag_member_decl_type(info);
  if (decl_type) {
    psx_type_t *member_type =
        type_with_funcptr_sig_merged(decl_type, member_funcptr_sig);
    deref->type = type_with_self_qualifiers(
        member_type, member_is_const, member_is_volatile);
    deref->type_state.is_scalar_ptr_member_lvalue =
        mem_is_ptr && mem_size > 0 && mem_array_len <= 0;
    sync_plain_scalar_result_metadata_from_type(deref, deref->type);
  } else {
    deref->fp_kind = mem_fp_kind;
    deref->is_unsigned = mem_is_unsigned ? 1 : 0;
    if (mem_is_bool) deref->is_unsigned = 0;
  }
  return deref;
}

node_t *psx_node_new_unary_deref_for(node_t *operand) {
  psx_type_t *result_type = type_from_deref_operand(operand);
  if (!result_type) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = operand;
    return result;
  }

  node_t *result = arena_alloc(sizeof(node_t));
  result->kind = ND_DEREF;
  result->lhs = operand;
  result->type = result_type;
  init_unary_deref_expr_state(result, operand);
  sync_plain_scalar_result_metadata_from_type(result, result_type);
  return result;
}

node_t *psx_node_new_subscript_deref_for(node_t *base, node_t *base_addr,
                                         node_t *scaled_offset,
                                         int elem_size, int inner_deref_size,
                                         int next_deref_size,
                                         const int *extra_strides,
                                         int extra_strides_count) {
  psx_type_t *base_type = psx_node_get_type(base);
  psx_type_t *fixed_result_type = type_from_subscript_base_type(
      base_type, elem_size, inner_deref_size, next_deref_size,
      extra_strides, extra_strides_count);
  int parent_vla_row = psx_node_vla_row_stride_frame_off(base);
  if (base_type && parent_vla_row != 0) {
    int parent_remaining = node_vla_strides_remaining(base);
    psx_type_t *vla_result_type = type_from_vla_subscript_result(
        base_type, fixed_result_type, elem_size, inner_deref_size,
        parent_vla_row, parent_remaining);
    if (vla_result_type) {
      node_t *result = arena_alloc(sizeof(node_t));
      result->kind = ND_DEREF;
      result->lhs = psx_node_new_binary(ND_ADD, base_addr, scaled_offset);
      result->type = vla_result_type;
      result->type_state.subscript_uses_base_address =
          inner_deref_size > 0 ? 1 : 0;
      if (parent_remaining > 0) {
        int parent_elem = 0;
        psx_node_pointer_stride_metadata(base, &parent_elem, NULL, NULL, NULL);
        if (parent_elem > 0)
          node_type_state_store_stride(result, parent_elem, parent_elem,
                                       NULL, 0);
      }
      sync_plain_scalar_result_metadata_from_type(result, result->type);
      return result;
    }
  }
  if (fixed_result_type && parent_vla_row == 0) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = psx_node_new_binary(ND_ADD, base_addr, scaled_offset);
    result->type = fixed_result_type;
    init_subscript_expr_state(result, next_deref_size,
                              extra_strides, extra_strides_count);
    sync_plain_scalar_result_metadata_from_type(result, result->type);
    return result;
  }
  psx_type_t *deref_result_type = type_from_deref_operand(base);
  if (base_type && deref_result_type && parent_vla_row == 0) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = psx_node_new_binary(ND_ADD, base_addr, scaled_offset);
    result->type = deref_result_type;
    init_subscript_expr_state(result, next_deref_size,
                              extra_strides, extra_strides_count);
    sync_plain_scalar_result_metadata_from_type(result, result->type);
    return result;
  }
  if (base_type) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = psx_node_new_binary(ND_ADD, base_addr, scaled_offset);
    return result;
  }
  node_t *result = arena_alloc(sizeof(node_t));
  result->kind = ND_DEREF;
  result->lhs = psx_node_new_binary(ND_ADD, base_addr, scaled_offset);
  return result;
}

node_t *psx_node_new_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                         int member_type_size, token_kind_t member_tag_kind,
                                         char *member_tag_name, int member_tag_len,
                                         int member_is_tag_pointer) {
  int size = member_type_size > 0 ? member_type_size : 4;
  psx_type_t *type = NULL;
  if (member_tag_kind != TK_EOF) {
    type = psx_type_new_tag(member_tag_kind, member_tag_name, member_tag_len,
                            0, size);
    if (member_is_tag_pointer)
      type = psx_type_new_pointer(type, size);
  } else {
    type = psx_type_new_integer(TK_INT, size, 0);
  }
  return (node_t *)new_lvar_symbol_node(
      (owner ? owner->offset : 0) + member_offset, owner, type);
}

node_t *psx_node_new_tag_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                             const tag_member_info_t *info) {
  psx_type_t *decl_type = (psx_type_t *)psx_tag_member_decl_type(info);
  psx_type_t *member_type = decl_type;
  if (decl_type) {
    member_type = type_with_funcptr_sig_merged(
        decl_type, psx_ctx_tag_member_funcptr_sig(info));
    int owner_is_const = lvar_self_is_const_qualified(owner);
    int owner_is_volatile = lvar_self_is_volatile_qualified(owner);
    member_type = type_with_self_qualifiers(
        member_type, owner_is_const, owner_is_volatile);
  }
  if (!member_type)
    member_type = psx_type_new_integer(
        psx_tag_member_decl_is_bool(info) ? TK_BOOL : TK_INT,
        psx_tag_member_decl_value_size(info),
        psx_tag_member_decl_is_unsigned(info));
  node_lvar_t *node = new_lvar_symbol_node(
      (owner ? owner->offset : 0) + member_offset, owner, member_type);
  if (info && info->bit_width > 0) {
    node->base.type_state.bit_width = (unsigned char)info->bit_width;
    node->base.type_state.bit_offset = (unsigned char)info->bit_offset;
    node->base.type_state.bit_is_signed = info->bit_is_signed ? 1 : 0;
  }
  return (node_t *)node;
}

node_t *psx_node_new_gvar_for(global_var_t *gv) {
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  node->base.kind = ND_GVAR;
  if (gv) {
    node->base.type = psx_gvar_get_decl_type(gv);
    node->name = gv->name;
    node->name_len = gv->name_len;
    node->is_thread_local = gv->is_thread_local ? 1 : 0;
  }
  return (node_t *)node;
}

node_t *psx_node_new_gvar_array_base_for(global_var_t *gv) {
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  node->base.kind = ND_GVAR;
  if (gv) {
    node->base.type = psx_gvar_get_decl_type(gv);
    node->name = gv->name;
    node->name_len = gv->name_len;
    node->is_thread_local = gv->is_thread_local ? 1 : 0;
  }
  return (node_t *)node;
}

node_t *psx_node_new_static_local_gvar_for(lvar_t *var, int type_size) {
  (void)type_size;
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  node->base.kind = ND_GVAR;
  if (var) {
    node->base.type = static_local_backing_decl_type(var);
    if (!node->base.type) node->base.type = psx_lvar_get_decl_type(var);
    node->name = var->static_global_name;
    node->name_len = var->static_global_name_len;
  }
  return (node_t *)node;
}

lvar_t *psx_node_lvar_symbol(node_t *node) {
  if (!node || node->kind != ND_LVAR) return NULL;
  node_lvar_t *lv = (node_lvar_t *)node;
  return lv->var ? lv->var : psx_decl_find_lvar_by_offset(lv->offset);
}

node_t *psx_node_clone_lvalue_with_lhs(node_t *target, node_t *lhs) {
  if (!target || !is_lvalue_clone_kind(target->kind)) return target;
  switch (target->kind) {
    case ND_LVAR: {
      node_lvar_t *clone = arena_alloc(sizeof(node_lvar_t));
      *clone = *(node_lvar_t *)target;
      clone->base.lhs = lhs;
      return (node_t *)clone;
    }
    case ND_GVAR: {
      node_gvar_t *clone = arena_alloc(sizeof(node_gvar_t));
      *clone = *(node_gvar_t *)target;
      clone->base.lhs = lhs;
      return (node_t *)clone;
    }
    case ND_STRING: {
      node_string_t *clone = arena_alloc(sizeof(node_string_t));
      *clone = *(node_string_t *)target;
      clone->base.lhs = lhs;
      return (node_t *)clone;
    }
    case ND_DEREF:
    {
      node_t *clone = arena_alloc(sizeof(node_t));
      *clone = *target;
      clone->lhs = lhs;
      return clone;
    }
    default:
      return target;
  }
}

static int lhs_is_bool_slot(node_t *lhs) {
  if (!lhs || (lhs->kind != ND_LVAR && lhs->kind != ND_DEREF && lhs->kind != ND_GVAR)) {
    return 0;
  }
  psx_type_t *type = psx_node_get_type(lhs);
  if (type && type->kind == PSX_TYPE_BOOL) return 1;
  if (lhs->kind == ND_DEREF && lhs->lhs &&
      psx_node_pointee_is_bool(lhs->lhs) &&
      (ps_node_type_size(lhs) <= 1 || ps_node_deref_size(lhs) <= 1)) {
    return 1;
  }
  return 0;
}

static int node_scalar_ptr_member_lvalue(node_t *node) {
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->kind != PSX_TYPE_POINTER) return 0;
  return node && node->kind == ND_DEREF &&
         node->type_state.is_scalar_ptr_member_lvalue;
}

int psx_node_scalar_ptr_member_lvalue(node_t *node) {
  return node_scalar_ptr_member_lvalue(node);
}

int psx_node_subscript_deref_uses_base_address(node_t *node) {
  if (!node || node->kind != ND_DEREF) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->kind == PSX_TYPE_ARRAY) return 1;
  return node->type_state.subscript_uses_base_address;
}

int psx_node_deref_decays_to_address(node_t *node) {
  if (!node || node->kind != ND_DEREF) return 0;
  psx_type_t *type = psx_node_get_type(node);
  return type && type->kind == PSX_TYPE_ARRAY;
}

psx_type_t *psx_node_row_decay_pointer_arith_type(node_t *node) {
  if (!node || (node->kind != ND_DEREF && node->kind != ND_ADDR)) return NULL;
  int ds = ps_node_deref_size(node);
  if (ds <= 0 || ps_node_type_size(node) <= ds) return NULL;

  psx_type_t *type = psx_node_get_type(node);
  psx_type_t *base = (type && type->kind == PSX_TYPE_ARRAY && type->base)
                         ? type->base
                         : NULL;
  if (!base) return NULL;

  psx_type_t *ptr = psx_type_new_pointer(base, ds);
  if (type) psx_type_copy_pointer_metadata(ptr, type);
  ptr->deref_size = ds;
  ptr->base_deref_size = ds;
  ptr->pointer_qual_levels = 1;
  ptr->ptr_array_pointee_bytes = 0;
  ptr->outer_stride = 0;
  ptr->mid_stride = 0;
  ptr->extra_strides_count = 0;
  for (int i = 0; i < 5; i++) ptr->extra_strides[i] = 0;
  ptr->vla_row_stride_frame_off = 0;
  ptr->vla_strides_remaining = 0;
  return ptr;
}

int psx_node_compound_literal_array_size(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_COMMA) return psx_node_compound_literal_array_size(node->rhs);
  if (node->kind != ND_ADDR) return 0;
  if (node->type_state.compound_literal_array_size > 0)
    return node->type_state.compound_literal_array_size;
  return 0;
}

int psx_node_bitfield_width(node_t *node) {
  return node ? node->type_state.bit_width : 0;
}

int psx_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed) {
  if (node && node->type_state.bit_width > 0) {
    if (bit_width) *bit_width = node->type_state.bit_width;
    if (bit_offset) *bit_offset = node->type_state.bit_offset;
    if (bit_is_signed) *bit_is_signed = node->type_state.bit_is_signed;
    return 1;
  }
  return 0;
}

int psx_node_value_is_pointer_like(node_t *node) {
  if (!node) return 0;
  if (node->type) return ps_node_is_pointer(node);
  if (ps_node_is_pointer(node)) return 1;
  if (psx_node_pointer_qual_levels(node) > 0) return 1;
  if (psx_node_scalar_ptr_member_lvalue(node)) return 1;
  return 0;
}

int psx_node_aggregate_value_size(node_t *node) {
  if (!node) return 0;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_pointer = 0;
  psx_node_get_tag_type(node, &tag_kind, &tag_name, &tag_len, &is_tag_pointer);
  if (is_tag_pointer || !psx_ctx_is_tag_aggregate_kind(tag_kind)) return 0;
  if (psx_node_value_is_pointer_like(node)) return 0;
  int size = ps_node_type_size(node);
  if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  return size > 0 ? size : 0;
}

int psx_node_vla_alloc_descriptor_info(node_t *node, int *descriptor_frame_off,
                                       int *row_stride_frame_off) {
  if (descriptor_frame_off) *descriptor_frame_off = 0;
  if (row_stride_frame_off) *row_stride_frame_off = 0;
  if (!node || node->kind != ND_VLA_ALLOC) return 0;
  node_vla_alloc_t *alloc = (node_vla_alloc_t *)node;
  if (descriptor_frame_off) *descriptor_frame_off = alloc->descriptor_frame_off;
  if (row_stride_frame_off) *row_stride_frame_off = alloc->row_stride_frame_off;
  return alloc->descriptor_frame_off > 0;
}

node_t *psx_node_new_vla_alloc(int descriptor_frame_off,
                               int row_stride_frame_off,
                               node_t *lhs, node_t *rhs) {
  node_vla_alloc_t *node = arena_alloc(sizeof(node_vla_alloc_t));
  node->base.kind = ND_VLA_ALLOC;
  node->base.lhs = lhs;
  node->base.rhs = rhs;
  node->descriptor_frame_off = descriptor_frame_off;
  node->row_stride_frame_off = row_stride_frame_off;
  return (node_t *)node;
}

node_t *psx_node_new_assign(node_t *lhs, node_t *rhs) {
  /* C11 6.5.16: 代入の RHS は void 型であってはならない。
   * direct / indirect call の違いは ND_FUNCALL の materialized type 側へ寄せる。 */
  if (rhs && rhs->kind == ND_FUNCALL) {
    node_func_t *fn = (node_func_t *)rhs;
    psx_type_t *rhs_type = psx_node_get_type(rhs);
    if (rhs_type && rhs_type->kind == PSX_TYPE_VOID) {
      if (fn->callee == NULL && fn->funcname) {
        psx_diag_ctx(tk_get_current_token(), "assign",
                     "void 戻り値関数の結果は代入/初期化に使えません: '%.*s' (C11 6.5.16)",
                     fn->funcname_len, fn->funcname);
      } else {
        psx_diag_ctx(tk_get_current_token(), "assign",
                     "void 戻り値関数の結果は代入/初期化に使えません (C11 6.5.16)");
      }
    }
  }
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_ASSIGN;
  node->lhs = lhs;
  node->rhs = (lhs_is_bool_slot(lhs) && rhs)
                  ? psx_node_new_binary(ND_NE, rhs, psx_node_new_num(0))
                  : rhs;
  node->type = psx_node_get_type(lhs);
  return node;
}

void psx_node_reject_const_assign(node_t *node, const char *op) {
  (void)op;
  if (!node) return;
  if (node->kind == ND_LVAR || node->kind == ND_GVAR || node->kind == ND_DEREF) {
    /* ag_c の慣習: ポインタ変数の is_const_qualified は「pointee の const」を
     * 表す (_Generic の判定等で利用)。「変数自身の const」は
     * pointer_const_qual_mask の bit 0 で保持される。
     * したがって p = q を拒否するのはこのビットが立っているときのみ
     * (`int * const p;` のケース)。非ポインタ変数は従来通り
     * is_const_qualified を見る (`const int x = 5; x = 10;` を拒否)。 */
    if (node_self_is_const_qualified(node)) {
      diag_emit_tokf(DIAG_ERR_PARSER_CONST_ASSIGNMENT, curtok(),
                     diag_message_for(DIAG_ERR_PARSER_CONST_ASSIGNMENT));
    }
  }
}

static int node_pointee_is_const(node_t *node) {
  if (!node) return 0;
  return psx_node_pointee_is_const_qualified(node);
}

void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs) return;
  if (lhs->kind != ND_LVAR && lhs->kind != ND_GVAR) return;
  if (!ps_node_is_pointer(lhs)) return;
  if (psx_node_pointee_is_const_qualified(lhs)) return;
  if (node_pointee_is_const(rhs)) {
    diag_emit_tokf(DIAG_ERR_PARSER_CONST_QUAL_DISCARD, curtok(),
                   diag_message_for(DIAG_ERR_PARSER_CONST_QUAL_DISCARD));
  }
}

void psx_node_expect_lvalue(node_t *node, const char *op) {
  if (!node || (node->kind != ND_LVAR && node->kind != ND_DEREF && node->kind != ND_GVAR)) {
    diag_emit_tokf(DIAG_ERR_PARSER_LVALUE_REQUIRED, curtok(),
                   diag_message_for(DIAG_ERR_PARSER_LVALUE_REQUIRED), (char *)op);
  }
}

void psx_node_expect_incdec_target(node_t *node, const char *op) {
  psx_node_expect_lvalue(node, op);
  psx_node_reject_const_assign(node, op);
  /* C11 6.5.2.4 / 6.5.3.1: ++ / -- の対象は実数型 (整数・浮動小数点) または
   * ポインタ型でよい。float / double も許可する。 */
}

node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op) {
  psx_node_expect_lvalue(lhs, op);
  psx_node_reject_const_assign(lhs, op);
  /* C11 6.5.16.2p3: `p += n` でポインタ算術するときは、rhs を要素サイズ倍に
   * スケーリングする。`add()` 経路と挙動を揃える。 */
  if ((op_kind == ND_ADD || op_kind == ND_SUB) && ps_node_is_pointer(lhs)) {
    int ds = ps_node_deref_size(lhs);
    if (ds > 1) {
      rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
    }
  }
  node_t *op_expr = psx_node_new_binary(op_kind, lhs, rhs);
  node_t *assign_node = psx_node_new_assign(lhs, op_expr);
  return (node_t *)assign_node;
}
