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

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
static inline token_t *curtok(void) { return tk_get_current_token(); }
static node_mem_t *node_mem_view(node_t *node);
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

static int pointer_view_from_node_direct(node_t *node, node_pointer_view_field_t field,
                                         int *value);
static int node_pointer_stride_from_type(const psx_type_t *type, int *inner_stride,
                                         int *next_stride, int *extra_strides,
                                         int *extra_strides_count);
static void sync_pointer_cast_mem_from_type(node_mem_t *mem, psx_type_t *type);
static int node_value_view_from_node_direct(node_t *node, node_value_view_field_t field,
                                            int *value);
static void sync_lvar_identifier_mem_from_decl_type(node_lvar_t *node,
                                                    const lvar_t *var,
                                                    psx_type_t *decl_type,
                                                    int is_pointer);
static int node_self_is_const_qualified(node_t *node);
static int node_self_is_volatile_qualified(node_t *node);
static int node_is_array_view(node_t *node);
static psx_type_t *type_decay_array_to_pointer(psx_type_t *array_type);
static int node_legacy_scalar_ptr_member(node_t *node);
static int node_scalar_ptr_member_lvalue(node_t *node);
static node_mem_t *node_legacy_pointee_scalar_ptr_mem(node_t *node);
static void sync_tag_member_mem_from_decl_type(node_mem_t *mem,
                                               const tag_member_info_t *info,
                                               psx_type_t *type);

static int is_mem_node_kind(node_kind_t kind) {
  return kind == ND_LVAR || kind == ND_GVAR || kind == ND_DEREF ||
         kind == ND_ASSIGN || kind == ND_ADDR || kind == ND_STRING ||
         kind == ND_CAST;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_mem(const node_mem_t *mem) {
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

static psx_decl_funcptr_sig_t funcptr_sig_from_lvar_raw(const lvar_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  return psx_decl_funcptr_sig_clone(src->funcptr_sig);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_gvar_raw(const global_var_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  return psx_decl_funcptr_sig_clone(src->funcptr_sig);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_lvar(const lvar_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  psx_type_t *type = lvar_decl_type_view(src);
  if (type && psx_decl_funcptr_sig_has_payload(type->funcptr_sig))
    return funcptr_sig_from_type(type);
  return funcptr_sig_from_lvar_raw(src);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_gvar(const global_var_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  psx_type_t *type = gvar_decl_type_view(src);
  if (type && psx_decl_funcptr_sig_has_payload(type->funcptr_sig))
    return funcptr_sig_from_type(type);
  return funcptr_sig_from_gvar_raw(src);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_tag_member(const tag_member_info_t *src) {
  return psx_ctx_tag_member_funcptr_sig(src);
}

static psx_decl_funcptr_sig_t funcptr_sig_merge_missing(psx_decl_funcptr_sig_t merged,
                                                        const psx_decl_funcptr_sig_t *sig,
                                                        int copy_variadic) {
  if (!sig) return merged;
  merged.function = psx_funcptr_type_shape_merge_missing(
      merged.function, sig->function, copy_variadic);
  return merged;
}

static void node_mem_store_funcptr_signature(node_mem_t *dst,
                                             const psx_decl_funcptr_sig_t *sig) {
  if (!dst || !sig) return;
  dst->funcptr_sig = psx_decl_funcptr_sig_clone(*sig);
}

void psx_node_store_funcptr_metadata(node_mem_t *dst, psx_decl_funcptr_sig_t sig) {
  node_mem_store_funcptr_signature(dst, &sig);
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

static void node_mem_merge_funcptr_signature(node_mem_t *dst,
                                             const psx_decl_funcptr_sig_t *sig,
                                             int copy_variadic) {
  if (!dst || !sig) return;
  dst->funcptr_sig = funcptr_sig_merge_missing(dst->funcptr_sig, sig, copy_variadic);
}

int psx_node_mem_has_funcptr_metadata(const node_mem_t *mem) {
  if (!mem) return 0;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_mem(mem);
  return psx_decl_funcptr_sig_has_payload(sig);
}

psx_decl_funcptr_sig_t psx_node_mem_funcptr_sig(const node_mem_t *mem) {
  return funcptr_sig_from_mem(mem);
}

static void type_copy_funcptr_metadata(psx_type_t *type, const node_mem_t *mem) {
  if (!type || !mem) return;
  type->funcptr_sig = funcptr_sig_from_mem(mem);
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

static int mem_tag_pointee_size(const node_mem_t *mem) {
  if (!mem || !psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) return 0;
  int fallback = mem->deref_size > 0 ? mem->deref_size : mem->base_deref_size;
  return tag_aggregate_size_from_ctx(mem->tag_kind, mem->tag_name, mem->tag_len,
                                     mem->tag_scope_depth_p1, fallback);
}

static int mem_pointee_scalar_is_bool(const node_mem_t *mem) {
  return mem && mem->pointee_is_bool ? 1 : 0;
}

static int mem_legacy_array_element_flag(const node_mem_t *mem, int scalar_flag) {
  if (!mem || !scalar_flag || mem->is_scalar_ptr_member) return 0;
  if (mem->is_pointer && mem->pointer_qual_levels > 0) return 0;
  if (mem->is_array_member) return 1;
  return mem->type_size > 0 && mem->deref_size > 0 &&
         mem->type_size >= mem->deref_size &&
         (mem->type_size % mem->deref_size) == 0;
}

static int mem_array_leaf_is_bool(const node_mem_t *mem) {
  return mem && (mem->pointee_is_bool ||
                 mem_legacy_array_element_flag(mem, mem->is_bool)) ? 1 : 0;
}

static int mem_array_leaf_is_unsigned(const node_mem_t *mem) {
  return mem && (mem->pointee_is_unsigned ||
                 mem_legacy_array_element_flag(mem, mem->is_unsigned)) ? 1 : 0;
}

static psx_type_t *type_new_pointee_base_from_mem(const node_mem_t *mem) {
  if (!mem) return NULL;
  if (mem->pointee_is_scalar_ptr) {
    int base_size = mem->base_deref_size > 0 ? mem->base_deref_size : 4;
    psx_type_t *base = mem->pointee_fp_kind != TK_FLOAT_KIND_NONE
                           ? psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind,
                                                base_size)
                           : psx_type_new_integer(mem_pointee_scalar_is_bool(mem)
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
    int size = mem_tag_pointee_size(mem);
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
  return psx_type_new_integer(mem_pointee_scalar_is_bool(mem) ? TK_BOOL : TK_EOF, sz,
                              mem->pointee_is_unsigned);
}

static psx_type_t *type_new_scalar_pointee_from_mem(const node_mem_t *mem,
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
    int tag_size = mem_tag_pointee_size(mem);
    if (tag_size <= 0) tag_size = scalar_size;
    return psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                            mem->tag_scope_depth_p1, tag_size);
  }
  return psx_type_new_integer(mem_pointee_scalar_is_bool(mem) ? TK_BOOL : TK_EOF,
                              scalar_size, mem->pointee_is_unsigned);
}

static psx_type_t *type_new_array_base_from_mem(const node_mem_t *mem) {
  if (mem && mem->ptr_array_pointee_bytes > 0 && mem->base_deref_size > 0 &&
      mem->pointee_is_scalar_ptr) {
    int scalar_size = mem->base_deref_size;
    psx_type_t *scalar = mem->pointee_fp_kind != TK_FLOAT_KIND_NONE
                             ? psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind,
                                                  scalar_size)
                             : psx_type_new_integer(mem_pointee_scalar_is_bool(mem)
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
    int tag_size = mem_tag_pointee_size(mem);
    psx_type_t *tag = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                                       mem->tag_scope_depth_p1, tag_size);
    psx_type_t *ptr = psx_type_new_pointer(tag, tag_size);
    ptr->pointer_qual_levels = mem->pointer_qual_levels > 0 ? mem->pointer_qual_levels : 1;
    return ptr;
  }
  if (mem && psx_ctx_is_tag_aggregate_kind(mem->tag_kind) &&
      mem->pointer_qual_levels > 0) {
    int tag_size = mem_tag_pointee_size(mem);
    psx_type_t *tag = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                                       mem->tag_scope_depth_p1, tag_size);
    psx_type_t *ptr = psx_type_new_pointer(tag, tag_size);
    ptr->pointer_qual_levels = mem->pointer_qual_levels;
    return ptr;
  }
  if ((mem_array_leaf_is_bool(mem) || mem_array_leaf_is_unsigned(mem)) &&
      mem->pointee_fp_kind == TK_FLOAT_KIND_NONE &&
      !psx_ctx_is_tag_aggregate_kind(mem->tag_kind)) {
    int sz = mem->base_deref_size > 0 ? mem->base_deref_size : mem->deref_size;
    if (sz <= 0 || sz > 8) sz = 1;
    return psx_type_new_integer(mem_array_leaf_is_bool(mem) ? TK_BOOL : TK_EOF,
                                sz, mem_array_leaf_is_unsigned(mem));
  }
  return type_new_pointee_base_from_mem(mem);
}

static psx_type_t *type_new_array_shape_from_mem(const node_mem_t *mem,
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
  psx_type_t *type = type_new_array_base_from_mem(mem);
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

static psx_type_t *type_ensure_inner_pointer_levels(psx_type_t *base,
                                                    const node_mem_t *mem) {
  if (!base || !mem || mem->pointer_qual_levels <= 1) return base;
  int base_deref = mem->base_deref_size > 0 ? mem->base_deref_size : mem->deref_size;
  if (base_deref <= 0) base_deref = psx_type_sizeof(base);
  if (base_deref <= 0) base_deref = 4;
  int depth = type_pointer_depth(base);
  while (depth < mem->pointer_qual_levels - 1) {
    int deref = depth == 0 ? base_deref : 8;
    psx_type_t *ptr = psx_type_new_pointer(base, deref);
    ptr->base_deref_size = base_deref;
    ptr->pointer_qual_levels = depth + 1;
    ptr->pointee_fp_kind = (tk_float_kind_t)mem->pointee_fp_kind;
    ptr->funcptr_sig = funcptr_sig_from_mem(mem);
    base = ptr;
    depth++;
  }
  return base;
}

static psx_type_t *type_pointer_array_base_from_mem(const node_mem_t *mem) {
  if (!mem || mem->inner_deref_size <= 0 || mem->ptr_array_pointee_bytes <= 0 ||
      mem->pointer_qual_levels < 2) {
    return NULL;
  }
  int scalar_size = mem->base_deref_size > 0 ? mem->base_deref_size
                                             : mem->deref_size;
  if (scalar_size <= 0) scalar_size = mem->pointee_is_bool ? 1 : 4;
  psx_type_t *scalar = type_new_scalar_pointee_from_mem(mem, scalar_size);
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

static psx_type_t *type_pointer_to_array_base_from_mem(const node_mem_t *mem) {
  if (!mem || mem->ptr_array_pointee_bytes <= 0 ||
      mem->pointer_qual_levels <= 0 || mem->type_size > 8 ||
      mem->compound_literal_array_size > 0) {
    return NULL;
  }
  int scalar_size = mem->base_deref_size > 0 ? mem->base_deref_size
                                             : mem->deref_size;
  if (scalar_size <= 0) scalar_size = mem->pointee_is_bool ? 1 : 4;
  psx_type_t *scalar = type_new_scalar_pointee_from_mem(mem, scalar_size);
  int points_to_pointer_element =
      mem->type_size == 8 &&
      mem->base_deref_size > 0 &&
      mem->base_deref_size < 8 &&
      mem->ptr_array_pointee_bytes > 8 &&
      (mem->ptr_array_pointee_bytes % 8) == 0;
  if (points_to_pointer_element) {
    psx_type_t *elem_ptr = psx_type_new_pointer(scalar, scalar_size);
    elem_ptr->base_deref_size = scalar_size;
    elem_ptr->pointer_qual_levels = 1;
    int row_len = mem->ptr_array_pointee_bytes / 8;
    if (row_len <= 0) row_len = 1;
    psx_type_t *row = psx_type_new_array(elem_ptr, row_len,
                                         mem->ptr_array_pointee_bytes,
                                         8, 0);
    row->base_deref_size = scalar_size;
    row->ptr_array_pointee_bytes = mem->ptr_array_pointee_bytes;
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
  row->ptr_array_pointee_bytes = mem->ptr_array_pointee_bytes;
  if (mem->pointer_qual_levels <= 1) return row;

  psx_type_t *row_ptr = psx_type_new_pointer(row, mem->ptr_array_pointee_bytes);
  row_ptr->base_deref_size = scalar_size;
  row_ptr->pointer_qual_levels = 1;
  row_ptr->outer_stride = mem->ptr_array_pointee_bytes;
  return row_ptr;
}

static psx_type_t *type_array_shape_base_from_mem(const node_mem_t *mem) {
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
    base = psx_type_new_integer(mem_array_leaf_is_bool(mem) ? TK_BOOL : TK_EOF,
                                scalar_size, mem_array_leaf_is_unsigned(mem));
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

static psx_type_t *type_from_mem(node_mem_t *mem, int force_array, int force_vla) {
  if (!mem) return NULL;
  if (mem->base.type) return mem->base.type;

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
    if (elem_size <= 0) elem_size = mem_tag_pointee_size(mem);
    if (elem_size <= 0) elem_size = mem->type_size;
    int array_len = (elem_size > 0 && mem->type_size > 0 &&
                     (mem->type_size % elem_size) == 0)
                        ? mem->type_size / elem_size
                        : 0;
    type = type_new_array_shape_from_mem(mem, elem_size, force_vla);
    int has_canonical_array_shape = type != NULL;
    if (!type) {
      psx_type_t *base = type_new_array_base_from_mem(mem);
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
  } else if (mem->is_pointer || mem->is_tag_pointer) {
    psx_type_t *base = type_pointer_array_base_from_mem(mem);
    int base_is_array_shape = base != NULL;
    if (!base) {
      base = type_pointer_to_array_base_from_mem(mem);
      base_is_array_shape = base && base->kind == PSX_TYPE_ARRAY;
    }
    if (!base) {
      base = type_array_shape_base_from_mem(mem);
      base_is_array_shape = base != NULL;
    }
    if (!base) base = type_new_pointee_base_from_mem(mem);
    int has_array_stride_shape =
        mem->inner_deref_size > 0 || mem->next_deref_size > 0 ||
        mem->extra_strides_count > 0;
    if (!has_array_stride_shape) {
      base = type_ensure_inner_pointer_levels(base, mem);
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
    type->fp_kind = mem->base.fp_kind;
  } else if (mem->base.fp_kind != TK_FLOAT_KIND_NONE) {
    type = psx_type_new_float((tk_float_kind_t)mem->base.fp_kind, mem->type_size);
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
    type_copy_funcptr_metadata(type, mem);
  }
  return type;
}

static psx_type_t *type_clone_persistent(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = calloc(1, sizeof(psx_type_t));
  if (!dst) return NULL;
  *dst = *src;
  dst->base = type_clone_persistent(src->base);
  dst->funcptr_sig = psx_decl_funcptr_sig_clone(src->funcptr_sig);
  return dst;
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
  return var && (var->pointee_is_bool || (var->is_array && var->is_bool)) ? 1 : 0;
}

static int lvar_pointee_is_unsigned(const lvar_t *var) {
  return var && (var->pointee_is_unsigned || (var->is_array && var->is_unsigned)) ? 1 : 0;
}

static void init_lvar_mem_from_legacy_fields(node_mem_t *mem,
                                             const lvar_t *var) {
  *mem = (node_mem_t){0};
  if (!var) return;
  mem->base.kind = ND_LVAR;
  mem->base.fp_kind = var->fp_kind;
  mem->base.is_unsigned = var->is_unsigned ? 1 : 0;
  mem->base.is_complex = var->is_complex ? 1 : 0;
  mem->base.is_atomic = var->is_atomic ? 1 : 0;
  mem->base.is_long_long = var->is_long_long ? 1 : 0;
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
  psx_decl_funcptr_sig_t funcptr_sig = funcptr_sig_from_lvar_raw(var);
  node_mem_store_funcptr_signature(mem, &funcptr_sig);
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
  if (var->is_array && var->pointer_qual_levels > 0 &&
      var->base_deref_size > 0 && var->elem_size > var->base_deref_size) {
    mem->pointee_is_scalar_ptr = 1;
  }
}

static void init_lvar_decl_source_mem(node_mem_t *mem, lvar_t *var) {
  init_lvar_mem_from_legacy_fields(mem, var);
}

static void init_lvar_ref_mem_from_var(node_mem_t *mem, const lvar_t *var) {
  init_lvar_mem_from_legacy_fields(mem, var);
}

static void sync_materialized_lvar_runtime_shape(lvar_t *var, psx_type_t *type) {
  if (!var || !type) return;
  if (var->is_vla) {
    type->is_vla = 1;
    type->vla_row_stride_frame_off = var->vla_row_stride_frame_off;
    type->vla_strides_remaining = var->vla_strides_remaining;
  }
  if ((type->kind == PSX_TYPE_ARRAY || type->kind == PSX_TYPE_POINTER) &&
      var->outer_stride > 0) {
    type->outer_stride = var->outer_stride;
  }
  if ((type->kind == PSX_TYPE_ARRAY || type->kind == PSX_TYPE_POINTER) &&
      var->mid_stride > 0) {
    type->mid_stride = var->mid_stride;
  }
  if ((type->kind == PSX_TYPE_ARRAY || type->kind == PSX_TYPE_POINTER) &&
      var->extra_strides_count > 0) {
    type->extra_strides_count = var->extra_strides_count;
    for (int i = 0; i < var->extra_strides_count && i < 5; i++) {
      type->extra_strides[i] = var->extra_strides ? var->extra_strides[i] : 0;
    }
  }
}

psx_type_t *psx_lvar_get_decl_type(lvar_t *var) {
  return lvar_decl_type_consistent(var);
}

psx_type_t *psx_lvar_materialize_decl_type(lvar_t *var) {
  if (!var) return NULL;
  if (var->decl_type) return var->decl_type;
  node_mem_t mem;
  init_lvar_decl_source_mem(&mem, var);
  if (var->is_byref_param && psx_ctx_is_tag_aggregate_kind(var->tag_kind) &&
      !var->is_tag_pointer && var->elem_size > 0) {
    mem.type_size = (short)var->elem_size;
  }
  int force_array = var->is_array || (var->is_vla && var->pointer_qual_levels == 0);
  var->decl_type = type_from_mem(&mem, force_array, force_array && var->is_vla);
  sync_materialized_lvar_runtime_shape(var, var->decl_type);
  if (var->decl_type) var->decl_type->type_sig = var->type_sig;
  return var->decl_type;
}

psx_type_t *psx_lvar_refresh_decl_type(lvar_t *var) {
  if (!var) return NULL;
  psx_decl_invalidate_lvar_decl_type(var);
  return psx_lvar_materialize_decl_type(var);
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
    return tag_aggregate_size_from_ctx(gv->tag_kind, gv->tag_name, gv->tag_len,
                                       gv->tag_scope_depth_p1,
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

static int type_array_outer_element_size(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  int total_size = psx_type_sizeof(type);
  if (total_size > 0 && type->array_len > 0 &&
      (total_size % type->array_len) == 0)
    return total_size / type->array_len;
  if (type->elem_size > 0) return type->elem_size;
  return psx_type_deref_size(type);
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

static void sync_pointee_flags_mem_from_type(node_mem_t *mem,
                                             const psx_type_t *type) {
  if (!mem) return;
  mem->pointee_is_unsigned = 0;
  mem->pointee_is_bool = 0;
  mem->pointee_is_void = 0;
  const psx_type_t *pointee_value_type = type_pointee_value_type(type);
  if (!pointee_value_type) return;
  mem->pointee_is_unsigned = psx_type_is_unsigned(pointee_value_type) ? 1 : 0;
  mem->pointee_is_bool = pointee_value_type->kind == PSX_TYPE_BOOL ? 1 : 0;
  mem->pointee_is_void = pointee_value_type->kind == PSX_TYPE_VOID ? 1 : 0;
  mem->is_const_qualified = pointee_value_type->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = pointee_value_type->is_volatile_qualified ? 1 : 0;
}

static void clear_pointer_payload_mem(node_mem_t *mem) {
  if (!mem) return;
  mem->is_pointer = 0;
  mem->is_scalar_ptr_member = 0;
  mem->is_pointer_const_qualified = 0;
  mem->is_pointer_volatile_qualified = 0;
  mem->pointer_const_qual_mask = 0;
  mem->pointer_volatile_qual_mask = 0;
  mem->pointer_qual_levels = 0;
  mem->base_deref_size = 0;
  mem->inner_deref_size = 0;
  mem->next_deref_size = 0;
  mem->extra_strides_count = 0;
  for (int i = 0; i < 5; i++) mem->extra_strides[i] = 0;
  mem->ptr_array_pointee_bytes = 0;
  mem->vla_row_stride_frame_off = 0;
  mem->vla_strides_remaining = 0;
}

static void sync_scalar_mem_from_decl_type(node_mem_t *mem,
                                           const psx_type_t *type) {
  if (!mem || !type || type_is_pointer_view_type(type)) return;
  mem->base.fp_kind =
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)
          ? type->fp_kind
          : TK_FLOAT_KIND_NONE;
  mem->base.is_unsigned = psx_type_is_unsigned(type) ? 1 : 0;
  mem->base.is_complex = type->kind == PSX_TYPE_COMPLEX ? 1 : 0;
  mem->base.is_atomic = type->is_atomic ? 1 : 0;
  mem->pointee_fp_kind = TK_FLOAT_KIND_NONE;
  mem->pointee_is_unsigned = 0;
  mem->pointee_is_bool = 0;
  mem->pointee_is_void = 0;
  clear_pointer_payload_mem(mem);
  mem->deref_size = 0;
  mem->is_unsigned = psx_type_is_unsigned(type) ? 1 : 0;
  mem->is_bool = type->kind == PSX_TYPE_BOOL ? 1 : 0;
  mem->is_complex = type->kind == PSX_TYPE_COMPLEX ? 1 : 0;
  mem->is_atomic = type->is_atomic ? 1 : 0;
  mem->is_long_long = type->is_long_long ? 1 : 0;
  mem->is_plain_char = type->is_plain_char ? 1 : 0;
  mem->is_long_double = type->is_long_double ? 1 : 0;
  mem->is_const_qualified = type->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = type->is_volatile_qualified ? 1 : 0;
}

static void sync_tag_mem_from_decl_type(node_mem_t *mem,
                                        const psx_type_t *type) {
  if (!mem || !type) return;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int tag_scope_depth_p1 = 0;
  int is_tag_pointer = 0;
  const psx_type_t *tag_type = NULL;
  if (type_is_pointer_view_type(type)) {
    const psx_type_t *pointee = type_pointee_value_type(type);
    if (psx_type_is_tag_aggregate(pointee)) {
      tag_type = pointee;
      is_tag_pointer = 1;
    }
  } else if (psx_type_is_tag_aggregate(type)) {
    tag_type = type;
  }
  if (tag_type) {
    tag_kind = tag_type->tag_kind;
    tag_name = tag_type->tag_name;
    tag_len = tag_type->tag_len;
    tag_scope_depth_p1 = tag_type->tag_scope_depth_p1;
  }
  mem->tag_kind = tag_kind;
  mem->tag_name = tag_name;
  mem->tag_len = tag_len;
  mem->tag_scope_depth_p1 = tag_scope_depth_p1;
  mem->is_tag_pointer = is_tag_pointer;
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
  psx_gvar_view_t view = psx_gvar_view(gv);
  gvar_aggregate_layout_t layout = {
      .tag_kind = view.tag_kind,
      .tag_name = view.tag_name,
      .tag_len = view.tag_len,
      .tag_scope_depth_p1 = gv ? gv->tag_scope_depth_p1 : 0,
      .type_size = view.type_size,
      .elem_size = view.type_size,
      .elem_count = 1,
      .is_array = view.is_array,
      .is_union = psx_gvar_is_union_aggregate(gv),
  };
  if (view.is_array) {
    layout.elem_size = psx_gvar_initializer_element_size(gv, view.type_size);
    layout.elem_count = psx_gvar_initializer_element_count(gv, view.type_size);
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
  if (!mi || !mi->decl_type || mi->decl_type->kind == PSX_TYPE_POINTER) return NULL;
  const psx_type_t *type = mi->decl_type;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return psx_type_is_tag_aggregate(type) ? type : NULL;
}

int psx_tag_member_is_struct_aggregate(const tag_member_info_t *mi) {
  const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
  if (leaf) return leaf->kind == PSX_TYPE_STRUCT;
  if (mi && mi->decl_type) return 0;
  return mi && !mi->is_tag_pointer && mi->tag_kind == TK_STRUCT;
}

int psx_tag_member_is_union_aggregate(const tag_member_info_t *mi) {
  const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
  if (leaf) return leaf->kind == PSX_TYPE_UNION;
  if (mi && mi->decl_type) return 0;
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
  return gv && (gv->pointee_is_bool || gv->elem_is_bool) ? 1 : 0;
}

static int gvar_pointee_is_unsigned(const global_var_t *gv) {
  return gv && (gv->pointee_is_unsigned || (gv->is_array && gv->is_unsigned)) ? 1 : 0;
}

static void init_gvar_decl_source_mem(node_mem_t *mem, global_var_t *gv) {
  *mem = (node_mem_t){0};
  if (!gv) return;
  mem->base.kind = ND_GVAR;
  mem->base.fp_kind = (tk_float_kind_t)gv->fp_kind;
  mem->base.is_unsigned = gv->is_unsigned ? 1 : 0;
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
  psx_decl_funcptr_sig_t funcptr_sig = funcptr_sig_from_gvar_raw(gv);
  node_mem_store_funcptr_signature(mem, &funcptr_sig);
  mem->pointer_qual_levels = gv->pointer_qual_levels;
  mem->inner_deref_size = (short)gv->outer_stride;
  mem->next_deref_size = (short)gv->mid_stride;
  mem->extra_strides_count = gv->extra_strides_count;
  for (int i = 0; i < gv->extra_strides_count && i < 5; i++) {
    mem->extra_strides[i] = gv->extra_strides[i];
  }
  mem->ptr_array_pointee_bytes = gv->ptr_array_pointee_bytes;
  if (gv->is_array && gv->pointee_elem_size > 0 && gv->tag_kind == TK_EOF) {
    mem->pointee_is_scalar_ptr = 1;
  }
}

psx_type_t *psx_gvar_get_decl_type(global_var_t *gv) {
  return gvar_decl_type_consistent(gv);
}

psx_type_t *psx_gvar_materialize_decl_type(global_var_t *gv) {
  if (!gv) return NULL;
  if (gv->decl_type) return gv->decl_type;
  node_mem_t mem;
  init_gvar_decl_source_mem(&mem, gv);
  psx_type_t *arena_type = type_from_mem(&mem, gv->is_array, 0);
  gv->decl_type = type_clone_persistent(arena_type);
  if (gv->decl_type) gv->decl_type->type_sig = gv->type_sig;
  return gv->decl_type;
}

psx_type_t *psx_gvar_refresh_decl_type(global_var_t *gv) {
  if (!gv) return NULL;
  psx_decl_invalidate_gvar_decl_type(gv);
  return psx_gvar_materialize_decl_type(gv);
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

static psx_type_t *type_wrap_ret_pointee_array_base(psx_type_t *base,
                                                    psx_ret_pointee_array_t ret_array) {
  if (!base || !psx_ret_pointee_array_has_dims(ret_array)) return base;
  int elem_size = ret_array.elem_size > 0 ? ret_array.elem_size : psx_type_sizeof(base);
  if (elem_size <= 0) return base;
  if (ret_array.second_dim > 0) {
    int inner_size = ret_array.second_dim * elem_size;
    psx_type_t *inner =
        psx_type_new_array(base, ret_array.second_dim, inner_size, elem_size, 0);
    inner->outer_stride = elem_size;
    int outer_size = ret_array.first_dim * inner_size;
    psx_type_t *outer =
        psx_type_new_array(inner, ret_array.first_dim, outer_size, inner_size, 0);
    outer->outer_stride = inner_size;
    outer->mid_stride = elem_size;
    return outer;
  }
  int array_size = ret_array.first_dim * elem_size;
  psx_type_t *array =
      psx_type_new_array(base, ret_array.first_dim, array_size, elem_size, 0);
  array->outer_stride = elem_size;
  return array;
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
  psx_type_t *pointee = type_wrap_ret_pointee_array_base(base, ret_array);
  int deref_size = levels >= 2 ? 8 : psx_type_sizeof(pointee);
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    int row = psx_ret_pointee_array_row_stride(ret_array);
    if (row > 0) deref_size = row;
  }
  psx_type_t *type = psx_type_new_pointer(pointee, deref_size);
  type->pointer_qual_levels = levels;
  type->base_deref_size = psx_type_sizeof(base);
  type_apply_pointee_qualifiers(type, ret.pointee_const_qualified,
                                ret.pointee_volatile_qualified);
  if (ret.is_funcptr) {
    type->funcptr_sig = psx_decl_funcptr_sig_clone(ret.funcptr_sig);
  }
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    type->funcptr_sig.function.callable.return_shape.pointee_array = ret_array;
  }
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
    type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
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

static int tag_view_from_mem(const node_mem_t *mem, node_tag_view_t *view) {
  node_tag_view_t out = node_tag_view_zero();
  if (!mem) {
    if (view) *view = out;
    return 0;
  }
  out.kind = mem->tag_kind;
  out.name = mem->tag_name;
  out.len = mem->tag_len;
  out.is_pointer = mem->is_tag_pointer;
  out.scope_depth_p1 = mem->tag_scope_depth_p1;
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
  if (node->type) {
    if (view) *view = typed;
    return 0;
  }
  return tag_view_from_mem(node_mem_view(node), view);
}

static node_mem_t *funcall_callee_mem(node_func_t *fn) {
  if (!fn || !fn->callee) return NULL;
  switch (fn->callee->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_CAST:
      return (node_mem_t *)fn->callee;
    default:
      return NULL;
  }
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
    psx_type_t *pointee = type_wrap_ret_pointee_array_base(base, ret_array);
    psx_type_t *type = psx_type_new_pointer(pointee, deref_size);
    if (callee_type->base) {
      type_apply_pointee_qualifiers(type, callee_type->base->is_const_qualified,
                                    callee_type->base->is_volatile_qualified);
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->funcptr_sig.function.callable.return_shape.pointee_array = ret_array;
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
      type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
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
  psx_type_t *from_callee_type = type_from_funcptr_callee_type(fn);
  if (from_callee_type) return from_callee_type;
  if (fn && fn->callee && fn->callee->type) return NULL;

  node_mem_t *cm = funcall_callee_mem(fn);
  if (!cm) return NULL;
  psx_decl_funcptr_sig_t callee_sig = funcptr_sig_from_mem(cm);
  if (callee_sig.function.callable.return_shape.is_void) return type_new_void();
  if (callee_sig.function.callable.return_shape.is_complex) {
    int complex_size = fn->base.fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, (tk_float_kind_t)fn->base.fp_kind,
                                  complex_size, 0, 1, 0);
  }

  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  psx_node_get_tag_type(fn->callee, &tag_kind, &tag_name, &tag_len, NULL);
  if (!callee_sig.function.callable.return_shape.is_data_pointer &&
      psx_ctx_is_tag_aggregate_kind(tag_kind)) {
    int size = fn->base.ret_struct_size;
    if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_type_new_tag(tag_kind, tag_name, tag_len,
                            psx_node_get_tag_scope_depth(fn->callee) + 1, size);
  }

  psx_ret_pointee_array_t ret_array = callee_sig.function.callable.return_shape.pointee_array;
  int returns_data_pointer =
      callee_sig.function.callable.return_shape.is_data_pointer || psx_ret_pointee_array_has_dims(ret_array);
  if (returns_data_pointer) {
    tk_float_kind_t ret_pointee_fp = (tk_float_kind_t)fn->base.fp_kind;
    if (ret_pointee_fp == TK_FLOAT_KIND_NONE)
      ret_pointee_fp = callee_sig.function.callable.return_shape.pointee_fp_kind;
    psx_type_t *base = NULL;
    if (psx_ctx_is_tag_aggregate_kind(tag_kind)) {
      int size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      base = psx_type_new_tag(tag_kind, tag_name, tag_len,
                              psx_node_get_tag_scope_depth(fn->callee) + 1, size);
    } else if (ret_pointee_fp != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float(ret_pointee_fp,
                                ret_pointee_fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    } else {
      int base_size = callee_sig.function.callable.return_shape.int_width > 0
                          ? callee_sig.function.callable.return_shape.int_width
                          : ret_array.elem_size;
      if (base_size <= 0)
        base_size = cm->base_deref_size > 0 ? cm->base_deref_size : cm->deref_size;
      if (base_size <= 0 || base_size > 8) base_size = 4;
      base = psx_type_new_integer(TK_EOF, base_size, cm->pointee_is_unsigned);
    }
    int deref_size = psx_type_sizeof(base);
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      int row = psx_ret_pointee_array_row_stride(ret_array);
      if (row > 0) deref_size = row;
    }
    psx_type_t *type = psx_type_new_pointer(base, deref_size);
    type_apply_pointee_qualifiers(type, cm->is_const_qualified,
                                  cm->is_volatile_qualified);
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->funcptr_sig.function.callable.return_shape.pointee_array = ret_array;
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
      type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
    }
    type->base_deref_size = psx_type_sizeof(base);
    type->pointer_qual_levels = psx_node_pointer_qual_levels(fn->callee) >= 2 ? 1 : 0;
    return type;
  }

  int width = callee_sig.function.callable.return_shape.int_width;
  if (width <= 0) {
    if (fn->base.fp_kind == TK_FLOAT_KIND_FLOAT) width = 4;
    else if (fn->base.fp_kind >= TK_FLOAT_KIND_DOUBLE) width = 8;
    else width = 4;
  }
  return type_from_scalar_shape(TK_EOF, (tk_float_kind_t)fn->base.fp_kind, width,
                                fn->base.is_unsigned, 0, width >= 8);
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
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return type_from_mem(as_mem(node), 0, 0);
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
    case ND_FP_TO_INT: {
      int width = ((node_mem_t *)node)->type_size;
      if (width <= 0) width = 4;
      return node->type = psx_type_new_integer(TK_INT, width, node->is_unsigned);
    }
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
    case ND_GVAR:
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
  node_mem_t *mem = node_mem_view(node);
  int s = psx_type_sizeof(type);
  if (s > 0) return s;
  if (mem && mem->type_size > 0) return mem->type_size;
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

static node_mem_t *node_mem_view(node_t *node) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
    case ND_VLA_ALLOC:
      return as_mem(node);
    default:
      return NULL;
  }
}

static int type_is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

static int type_is_pointer_to_array_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER &&
         type->base && type->base->kind == PSX_TYPE_ARRAY;
}

static int type_is_array_of_pointer_to_array(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_ARRAY &&
         type_is_pointer_to_array_type(type->base);
}

static int type_is_array_of_tag_aggregate(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_ARRAY &&
         psx_type_is_tag_aggregate(type->base);
}

static int type_carries_ptr_array_pointee_after_deref(const psx_type_t *type) {
  return type_is_array_of_pointer_to_array(type) ||
         type_is_array_of_tag_aggregate(type);
}

static tk_float_kind_t type_deep_pointee_fp_kind(const psx_type_t *type) {
  if (!type_is_pointer_view_type(type)) return TK_FLOAT_KIND_NONE;
  const psx_type_t *cur = type;
  while (type_is_pointer_view_type(cur)) {
    if (cur->pointee_fp_kind != TK_FLOAT_KIND_NONE) return cur->pointee_fp_kind;
    cur = cur->base;
  }
  return cur && cur->kind == PSX_TYPE_FLOAT ? cur->fp_kind : TK_FLOAT_KIND_NONE;
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

static int scalar_flag_from_mem(const node_mem_t *mem, node_scalar_flag_t flag) {
  if (!mem) return 0;
  switch (flag) {
    case NODE_SCALAR_UNSIGNED:
      return mem->is_unsigned ? 1 : 0;
    case NODE_SCALAR_LONG_LONG:
      return mem->is_long_long ? 1 : 0;
    case NODE_SCALAR_PLAIN_CHAR:
      return mem->is_plain_char ? 1 : 0;
    case NODE_SCALAR_LONG_DOUBLE:
      return mem->is_long_double ? 1 : 0;
    default:
      return 0;
  }
}

static int scalar_flag_from_node_fallback(node_t *node, node_scalar_flag_t flag) {
  node_mem_t *mem = node_mem_view(node);
  if (mem) return scalar_flag_from_mem(mem, flag);
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
  if (node->type) return scalar_flag_from_type(type, flag);
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

static int pointee_flag_from_mem(const node_mem_t *mem, node_pointee_flag_t flag) {
  if (!mem) return 0;
  switch (flag) {
    case NODE_POINTEE_UNSIGNED:
      return mem->pointee_is_unsigned ? 1 : 0;
    case NODE_POINTEE_BOOL:
      return mem->pointee_is_bool ? 1 : 0;
    case NODE_POINTEE_VOID:
      return mem->pointee_is_void ? 1 : 0;
    case NODE_POINTEE_CONST:
      return mem->is_const_qualified ? 1 : 0;
    case NODE_POINTEE_VOLATILE:
      return mem->is_volatile_qualified ? 1 : 0;
    default:
      return 0;
  }
}

static int pointee_flag_from_node_direct(node_t *node, node_pointee_flag_t flag) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type_is_pointer_view_type(type) && type->base)
    return pointee_flag_from_type(type, flag);
  if (node->type) return 0;
  return pointee_flag_from_mem(node_mem_view(node), flag);
}

static int type_pointer_view_base_deref_size(const psx_type_t *type,
                                             int allow_sizeof_base_fallback) {
  if (!type_is_pointer_view_type(type)) return 0;
  int base_deref_size = type->base_deref_size;
  if (base_deref_size <= 0 && type->kind == PSX_TYPE_ARRAY) {
    base_deref_size = psx_type_deref_size(type);
  }
  if (base_deref_size <= 0 && allow_sizeof_base_fallback && type->base) {
    base_deref_size = psx_type_sizeof(type->base);
  }
  return base_deref_size;
}

static int pointer_view_from_type(const psx_type_t *type, node_pointer_view_field_t field,
                                  int *value) {
  if (!type_is_pointer_view_type(type)) return 0;
  switch (field) {
    case NODE_POINTER_QUAL_LEVELS:
      if (value) {
        int levels = type->pointer_qual_levels;
        if (levels <= 0 && type->kind == PSX_TYPE_POINTER) {
          levels = type_pointer_depth(type);
          if (levels <= 0) levels = 1;
        }
        *value = levels;
      }
      return 1;
    case NODE_POINTER_BASE_DEREF_SIZE:
      {
        int base_deref_size = type_pointer_view_base_deref_size(type, 0);
        if (base_deref_size <= 0) return 0;
        if (value) *value = base_deref_size;
      }
      return 1;
    case NODE_POINTER_PTR_ARRAY_POINTEE_BYTES:
      if (type->ptr_array_pointee_bytes <= 0) return 0;
      if (value) *value = type->ptr_array_pointee_bytes;
      return 1;
    case NODE_POINTER_CONST_MASK:
      if (value) *value = (int)type->pointer_const_qual_mask;
      return 1;
    case NODE_POINTER_VOLATILE_MASK:
      if (value) *value = (int)type->pointer_volatile_qual_mask;
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

static int pointer_view_from_mem(const node_mem_t *mem, node_pointer_view_field_t field,
                                 int *value) {
  if (!mem) return 0;
  switch (field) {
    case NODE_POINTER_QUAL_LEVELS:
      if (value) *value = mem->pointer_qual_levels;
      return 1;
    case NODE_POINTER_BASE_DEREF_SIZE:
      if (mem->base_deref_size <= 0) return 0;
      if (value) *value = mem->base_deref_size;
      return 1;
    case NODE_POINTER_PTR_ARRAY_POINTEE_BYTES:
      if (mem->ptr_array_pointee_bytes <= 0) return 0;
      if (value) *value = mem->ptr_array_pointee_bytes;
      return 1;
    case NODE_POINTER_CONST_MASK:
      if (value) *value = (int)mem->pointer_const_qual_mask;
      return 1;
    case NODE_POINTER_VOLATILE_MASK:
      if (value) *value = (int)mem->pointer_volatile_qual_mask;
      return 1;
    case NODE_POINTER_POINTEE_FP_KIND:
      if (value) *value = (int)mem->pointee_fp_kind;
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
  if (node->type) return 0;
  return pointer_view_from_mem(node_mem_view(node), field, value);
}

static int vla_view_from_type(const psx_type_t *type, node_vla_view_field_t field,
                              int *value) {
  if (!type_is_pointer_view_type(type)) return 0;
  switch (field) {
    case NODE_VLA_ROW_STRIDE_FRAME_OFF:
      if (type->vla_row_stride_frame_off == 0) return 0;
      if (value) *value = type->vla_row_stride_frame_off;
      return 1;
    case NODE_VLA_STRIDES_REMAINING:
      if (type->vla_strides_remaining <= 0) return 0;
      if (value) *value = type->vla_strides_remaining;
      return 1;
    default:
      return 0;
  }
}

static int vla_view_from_mem(const node_mem_t *mem, node_vla_view_field_t field,
                             int *value) {
  if (!mem) return 0;
  switch (field) {
    case NODE_VLA_ROW_STRIDE_FRAME_OFF:
      if (mem->vla_row_stride_frame_off == 0) return 0;
      if (value) *value = mem->vla_row_stride_frame_off;
      return 1;
    case NODE_VLA_STRIDES_REMAINING:
      if (mem->vla_strides_remaining <= 0) return 0;
      if (value) *value = mem->vla_strides_remaining;
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
  if (node->type) return 0;
  return vla_view_from_mem(node_mem_view(node), field, value);
}

static int mem_has_contextual_row_deref_size(const node_mem_t *mem) {
  if (!mem || mem->deref_size <= 0) return 0;
  if (mem->is_pointer &&
      (mem->inner_deref_size > 0 || mem->next_deref_size > 0 ||
       mem->extra_strides_count > 0 || mem->ptr_array_pointee_bytes > 0)) {
    return 1;
  }
  if (mem->type_size <= mem->deref_size) return 0;
  return mem->is_pointer || mem->inner_deref_size > 0 ||
         mem->next_deref_size > 0 || mem->extra_strides_count > 0 ||
         mem->ptr_array_pointee_bytes > 0;
}

static int node_value_view_from_mem(const node_mem_t *mem, node_value_view_field_t field,
                                    int *value) {
  if (!mem) return 0;
  switch (field) {
    case NODE_VALUE_TYPE_SIZE:
      if (value) *value = mem->type_size;
      return 1;
    case NODE_VALUE_DEREF_SIZE:
      if (value) *value = mem->deref_size;
      return 1;
    case NODE_VALUE_IS_POINTER:
      if (value) *value = mem->is_pointer ? 1 : 0;
      return 1;
    default:
      return 0;
  }
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
  node_mem_t *mem = node_mem_view(node);
  if (node->type) {
    if (field == NODE_VALUE_DEREF_SIZE && type_is_pointer_view_type(type) &&
        mem_has_contextual_row_deref_size(mem))
      return node_value_view_from_mem(mem, field, value);
    if (node_value_view_from_type(type, field, value,
                                  field == NODE_VALUE_TYPE_SIZE))
      return 1;
    return 0;
  }
  if (field == NODE_VALUE_DEREF_SIZE && node->kind == ND_GVAR)
    return node_value_view_from_mem(mem, field, value);
  if (field == NODE_VALUE_IS_POINTER) {
    if (type) {
      if (value) *value = psx_type_is_pointer(type) ? 1 : 0;
      return 1;
    }
    int mem_is_ptr = 0;
    int has_mem = node_value_view_from_mem(mem, field, &mem_is_ptr);
    if (value) *value = mem_is_ptr ? 1 : 0;
    return has_mem;
  }
  if (node_value_view_from_type(type, field, value,
                                1))
    return 1;
  if (node_value_view_from_mem(mem, field, value)) return 1;
  return 0;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_node(node_t *node, int copy_variadic) {
  if (!node) return (psx_decl_funcptr_sig_t){0};
  psx_decl_funcptr_sig_t sig = {0};
  psx_type_t *type = psx_node_get_type(node);
  if (type) sig = funcptr_sig_merge_missing(sig, &type->funcptr_sig, copy_variadic);
  if (node->type) return sig;
  node_mem_t *mem = node_mem_view(node);
  if (mem) sig = funcptr_sig_merge_missing(sig, &mem->funcptr_sig, copy_variadic);
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

void psx_node_copy_funcptr_metadata(node_mem_t *dst, node_t *src) {
  if (!dst || !src) return;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_node(src, 0);
  node_mem_merge_funcptr_signature(dst, &sig, 0);
}

void psx_node_copy_funcptr_metadata_from_lvar(node_mem_t *dst, const lvar_t *src) {
  if (!dst || !src) return;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_lvar(src);
  node_mem_store_funcptr_signature(dst, &sig);
}

void psx_node_copy_funcptr_metadata_from_gvar(node_mem_t *dst, const global_var_t *src) {
  if (!dst || !src) return;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_gvar(src);
  node_mem_store_funcptr_signature(dst, &sig);
}

void psx_node_merge_funcptr_metadata_from_lvar(node_mem_t *dst, const lvar_t *src) {
  if (!dst || !src) return;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_lvar(src);
  node_mem_merge_funcptr_signature(dst, &sig, 1);
  if (dst->pointee_fp_kind == TK_FLOAT_KIND_NONE) {
    dst->pointee_fp_kind = (unsigned int)src->pointee_fp_kind;
  }
}

void psx_node_merge_funcptr_metadata_from_gvar(node_mem_t *dst, const global_var_t *src) {
  if (!dst || !src) return;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_gvar(src);
  node_mem_merge_funcptr_signature(dst, &sig, 1);
  if (dst->pointee_fp_kind == TK_FLOAT_KIND_NONE) {
    dst->pointee_fp_kind = (unsigned int)src->pointee_fp_kind;
  }
}

void psx_node_copy_funcptr_metadata_from_tag_member(node_mem_t *dst,
                                                    const tag_member_info_t *src) {
  if (!dst || !src) return;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_tag_member(src);
  node_mem_store_funcptr_signature(dst, &sig);
}

static int gvar_ref_deref_size_from_type(const global_var_t *gv,
                                         const psx_type_t *decl_type) {
  (void)gv;
  if (!decl_type || !psx_type_is_pointer(decl_type)) return 0;
  if (decl_type->kind == PSX_TYPE_POINTER &&
      decl_type->ptr_array_pointee_bytes > 0) {
    int base_size = decl_type->base ? psx_type_sizeof(decl_type->base) : 0;
    if (base_size > 0 && decl_type->base_deref_size > 0 &&
        base_size > decl_type->base_deref_size) {
      return base_size;
    }
    return decl_type->ptr_array_pointee_bytes;
  }
  if (decl_type->kind == PSX_TYPE_POINTER && decl_type->outer_stride > 0) {
    return decl_type->outer_stride;
  }
  int deref_size = psx_type_deref_size(decl_type);
  return deref_size;
}

static void sync_gvar_ref_mem_from_decl_type(node_mem_t *mem,
                                             const global_var_t *gv,
                                             psx_type_t *decl_type) {
  if (!mem || !decl_type) return;
  mem->base.type = decl_type;
  int type_size = psx_type_sizeof(decl_type);
  if (type_size > 0) mem->type_size = (short)type_size;
  mem->is_pointer = psx_type_is_pointer(decl_type) ? 1 : 0;

  sync_scalar_mem_from_decl_type(mem, decl_type);
  sync_pointer_cast_mem_from_type(mem, decl_type);

  int deref_size = gvar_ref_deref_size_from_type(gv, decl_type);
  if (deref_size > 0) mem->deref_size = (short)deref_size;

  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (node_pointer_stride_from_type(decl_type, &inner_stride, &next_stride,
                                    extra_strides, &extra_count)) {
    mem->inner_deref_size = (short)inner_stride;
    mem->next_deref_size = (short)next_stride;
    mem->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      mem->extra_strides[i] = extra_strides[i];
    for (int i = extra_count; i < 5; i++) mem->extra_strides[i] = 0;
  }

  int base_deref_size = type_pointer_view_base_deref_size(decl_type, 1);
  if (base_deref_size > 0) mem->base_deref_size = (short)base_deref_size;
  if (decl_type->pointer_qual_levels > 0)
    mem->pointer_qual_levels = (unsigned char)decl_type->pointer_qual_levels;
  if (decl_type->ptr_array_pointee_bytes > 0)
    mem->ptr_array_pointee_bytes = decl_type->ptr_array_pointee_bytes;

  mem->pointee_fp_kind = (unsigned int)type_deep_pointee_fp_kind(decl_type);
  sync_pointee_flags_mem_from_type(mem, decl_type);
  if (psx_decl_funcptr_sig_has_payload(decl_type->funcptr_sig))
    node_mem_store_funcptr_signature(mem, &decl_type->funcptr_sig);
  sync_tag_mem_from_decl_type(mem, decl_type);
}

void psx_node_init_gvar_ref_metadata(node_mem_t *mem, const global_var_t *gv) {
  if (!mem) return;
  *mem = (node_mem_t){0};
  mem->base.kind = ND_GVAR;
  if (!gv) return;
  psx_type_t *decl_type = psx_gvar_get_decl_type((global_var_t *)gv);
  mem->base.type = decl_type;
  mem->type_size = (short)gv->type_size;
  mem->deref_size = gv->deref_size;
  if (gv->outer_stride > 0 && !gv->is_array) {
    mem->deref_size = (short)gv->outer_stride;
    if (gv->mid_stride > 0) {
      mem->inner_deref_size = (short)gv->mid_stride;
      if (gv->extra_strides_count > 0) {
        mem->next_deref_size = (short)gv->extra_strides[0];
        for (int i = 1; i < gv->extra_strides_count && (i - 1) < 5; i++) {
          mem->extra_strides[i - 1] = gv->extra_strides[i];
        }
        mem->extra_strides[gv->extra_strides_count - 1] = (short)gv->deref_size;
        mem->extra_strides_count = gv->extra_strides_count;
      } else {
        mem->next_deref_size = (short)gv->deref_size;
      }
    } else {
      mem->inner_deref_size = (short)gv->deref_size;
    }
  }
  if (gv->ptr_array_pointee_bytes > 0) {
    mem->ptr_array_pointee_bytes = gv->ptr_array_pointee_bytes;
    mem->base_deref_size = gv->pointee_elem_size > 0 ? gv->pointee_elem_size : gv->deref_size;
    if (gv->outer_stride <= 0) mem->deref_size = 8;
  }
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
  mem->is_const_qualified = gv->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = gv->is_volatile_qualified ? 1 : 0;
  if (gv->pointer_qual_levels >= 2 && gv->outer_stride == 0) {
    if (gv->ptr_array_pointee_bytes <= 0 || mem->base_deref_size <= 0)
      mem->base_deref_size = gv->deref_size;
    mem->deref_size = 8;
    mem->pointer_qual_levels = gv->pointer_qual_levels;
  }
  mem->base.fp_kind = gv->fp_kind;
  mem->pointee_fp_kind = gv->pointee_fp_kind;
  psx_node_copy_funcptr_metadata_from_gvar(mem, gv);
  mem->is_bool = gv->is_bool ? 1 : 0;
  mem->is_unsigned = gv->is_unsigned ? 1 : 0;
  mem->pointee_is_bool = gvar_pointee_is_bool(gv);
  mem->pointee_is_unsigned = gvar_pointee_is_unsigned(gv);
  mem->is_long_double = gv->is_long_double ? 1 : 0;
  sync_gvar_ref_mem_from_decl_type(mem, gv, decl_type);
}

void psx_node_init_gvar_array_base_metadata(node_mem_t *mem, const global_var_t *gv) {
  if (!mem) return;
  *mem = (node_mem_t){0};
  mem->base.kind = ND_GVAR;
  if (!gv) return;
  psx_type_t *decl_type = psx_gvar_get_decl_type((global_var_t *)gv);
  mem->base.type = decl_type;
  mem->type_size = (short)gv->type_size;
  mem->deref_size = gv->deref_size;
  mem->tag_kind = gv->tag_kind;
  mem->tag_name = gv->tag_name;
  mem->tag_len = gv->tag_len;
  mem->tag_scope_depth_p1 = gv->tag_scope_depth_p1;
  mem->is_const_qualified = gv->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = gv->is_volatile_qualified ? 1 : 0;
  sync_gvar_ref_mem_from_decl_type(mem, gv, decl_type);
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

void psx_node_init_static_local_gvar_ref_metadata(node_mem_t *mem, const lvar_t *var,
                                                  int type_size) {
  if (!mem) return;
  *mem = (node_mem_t){0};
  mem->base.kind = ND_GVAR;
  if (!var) return;
  psx_type_t *backing_type = static_local_backing_decl_type(var);
  mem->base.type = backing_type ? backing_type
                                : psx_lvar_get_decl_type((lvar_t *)var);
  int sz = type_size > 0 ? type_size : (var->size > 0 ? var->size : var->elem_size);
  int deref = var->elem_size > 0 ? var->elem_size : sz;
  mem->type_size = (short)sz;
  mem->deref_size = (short)deref;
  mem->base.fp_kind = var->fp_kind;
  mem->base.is_unsigned = var->is_unsigned ? 1 : 0;
  mem->is_unsigned = var->is_unsigned ? 1 : 0;
  mem->is_bool = var->is_bool ? 1 : 0;
  mem->is_long_double = var->is_long_double ? 1 : 0;
  psx_node_copy_funcptr_metadata_from_lvar(mem, var);
  mem->tag_kind = var->tag_kind;
  mem->tag_name = var->tag_name;
  mem->tag_len = var->tag_len;
  mem->tag_scope_depth_p1 = var->tag_scope_depth_p1;
  mem->is_tag_pointer = 0;
  mem->is_const_qualified = var->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = var->is_volatile_qualified ? 1 : 0;
  if (var->size > 0 && sz > var->elem_size && var->elem_size > 0) mem->is_pointer = 1;
  global_var_t *backing = static_local_backing_gvar(var);
  if (backing && backing_type) {
    sync_gvar_ref_mem_from_decl_type(mem, backing, backing_type);
  } else if (mem->base.type) {
    sync_scalar_mem_from_decl_type(mem, mem->base.type);
    sync_pointer_cast_mem_from_type(mem, mem->base.type);
    int canonical_type_size = psx_type_sizeof(mem->base.type);
    if (canonical_type_size > 0) mem->type_size = (short)canonical_type_size;
    if (psx_decl_funcptr_sig_has_payload(mem->base.type->funcptr_sig))
      node_mem_store_funcptr_signature(mem, &mem->base.type->funcptr_sig);
    sync_tag_mem_from_decl_type(mem, mem->base.type);
  }
}

static void init_lvar_array_addr_strides(node_mem_t *addr, const lvar_t *var) {
  if (!addr || !var) return;
  int stride = (var->outer_stride > 0) ? var->outer_stride : var->elem_size;
  addr->type_size = (short)stride;
  addr->deref_size = (short)stride;
  addr->ptr_array_pointee_bytes = var->ptr_array_pointee_bytes;
  addr->pointer_qual_levels = var->pointer_qual_levels;
  addr->base_deref_size = var->base_deref_size;
  if (var->outer_stride > 0) {
    if (var->mid_stride > 0) {
      addr->inner_deref_size = (short)var->mid_stride;
      if (var->extra_strides_count > 0) {
        addr->next_deref_size = (short)var->extra_strides[0];
        for (int i = 1; i < var->extra_strides_count && (i - 1) < 5; i++) {
          addr->extra_strides[i - 1] = var->extra_strides[i];
        }
        addr->extra_strides[var->extra_strides_count - 1] = (short)var->elem_size;
        addr->extra_strides_count = var->extra_strides_count;
      } else {
        addr->next_deref_size = (short)var->elem_size;
      }
    } else {
      addr->inner_deref_size = (short)var->elem_size;
    }
  }
}

static void init_gvar_array_addr_strides(node_mem_t *addr, const global_var_t *gv) {
  if (!addr || !gv) return;
  int stride = (gv->outer_stride > 0) ? gv->outer_stride : gv->deref_size;
  addr->type_size = (short)stride;
  addr->deref_size = (short)stride;
  if (gv->outer_stride > 0) {
    if (gv->mid_stride > 0) {
      addr->inner_deref_size = (short)gv->mid_stride;
      if (gv->extra_strides_count > 0) {
        addr->next_deref_size = (short)gv->extra_strides[0];
        for (int i = 1; i < gv->extra_strides_count && (i - 1) < 5; i++) {
          addr->extra_strides[i - 1] = gv->extra_strides[i];
        }
        addr->extra_strides[gv->extra_strides_count - 1] = (short)gv->deref_size;
        addr->extra_strides_count = gv->extra_strides_count;
      } else {
        addr->next_deref_size = (short)gv->deref_size;
      }
    } else {
      addr->inner_deref_size = (short)gv->deref_size;
    }
  }
}

static void apply_array_addr_decl_type(node_mem_t *addr, psx_type_t *array_type) {
  if (!addr || !array_type) return;
  psx_type_t *view = array_type;
  if (array_type->kind == PSX_TYPE_ARRAY) {
    view = type_decay_array_to_pointer(array_type);
  } else if (!type_is_pointer_view_type(array_type)) {
    return;
  }
  if (!view) return;
  addr->base.type = view;
  clear_pointer_payload_mem(addr);
  addr->is_pointer = 1;
  int deref_size = psx_type_deref_size(view);
  if (deref_size > 0) addr->deref_size = (short)deref_size;
  addr->pointee_fp_kind = (unsigned int)type_deep_pointee_fp_kind(view);
  sync_pointee_flags_mem_from_type(addr, view);
  sync_tag_mem_from_decl_type(addr, view);
  if (view->pointer_qual_levels > 0)
    addr->pointer_qual_levels = (unsigned char)view->pointer_qual_levels;
  if (view->ptr_array_pointee_bytes > 0)
    addr->ptr_array_pointee_bytes = view->ptr_array_pointee_bytes;
  int base_deref_size = type_pointer_view_base_deref_size(view, 1);
  if (base_deref_size > 0)
    addr->base_deref_size = (short)base_deref_size;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (node_pointer_stride_from_type(view, &inner_stride, &next_stride,
                                    extra_strides, &extra_count)) {
    addr->inner_deref_size = (short)inner_stride;
    addr->next_deref_size = (short)next_stride;
    addr->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      addr->extra_strides[i] = extra_strides[i];
  }
}

static void init_lvar_array_addr_metadata_with_decl_type(node_mem_t *addr,
                                                         const lvar_t *var,
                                                         int is_tag_pointer,
                                                         psx_type_t *array_type) {
  if (!addr || !var) return;
  init_lvar_array_addr_strides(addr, var);
  psx_type_t *source_type =
      array_type ? array_type : psx_lvar_get_decl_type((lvar_t *)var);
  apply_array_addr_decl_type(addr, source_type);
  addr->is_pointer = 1;
  if (!addr->base.type) {
    addr->pointee_fp_kind = var->pointee_fp_kind != TK_FLOAT_KIND_NONE
                               ? (unsigned int)var->pointee_fp_kind
                               : (unsigned int)var->fp_kind;
  }
  addr->pointee_is_bool = lvar_pointee_is_bool(var);
  addr->pointee_is_unsigned = lvar_pointee_is_unsigned(var);
  if (addr->base.type)
    sync_pointee_flags_mem_from_type(addr, addr->base.type);
  psx_node_copy_funcptr_metadata_from_lvar(addr, var);
  addr->tag_kind = var->tag_kind;
  addr->tag_name = var->tag_name;
  addr->tag_len = var->tag_len;
  addr->tag_scope_depth_p1 = var->tag_scope_depth_p1;
  addr->is_tag_pointer = is_tag_pointer ? 1 : 0;
  if (addr->base.type)
    sync_tag_mem_from_decl_type(addr, addr->base.type);
  addr->is_const_qualified = var->is_const_qualified ? 1 : 0;
  addr->is_volatile_qualified = var->is_volatile_qualified ? 1 : 0;
}

void psx_node_init_lvar_array_addr_metadata(node_mem_t *addr, const lvar_t *var,
                                            int is_tag_pointer) {
  init_lvar_array_addr_metadata_with_decl_type(addr, var, is_tag_pointer, NULL);
}

void psx_node_init_gvar_array_addr_metadata(node_mem_t *addr, const global_var_t *gv) {
  if (!addr || !gv) return;
  psx_type_t *array_type = psx_gvar_get_decl_type((global_var_t *)gv);
  addr->base.type = type_decay_array_to_pointer(array_type);
  addr->tag_kind = gv->tag_kind;
  addr->tag_name = gv->tag_name;
  addr->tag_len = gv->tag_len;
  addr->tag_scope_depth_p1 = gv->tag_scope_depth_p1;
  addr->is_const_qualified = gv->is_const_qualified ? 1 : 0;
  addr->is_volatile_qualified = gv->is_volatile_qualified ? 1 : 0;
  if (gv->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
  init_gvar_array_addr_strides(addr, gv);
  addr->is_pointer = 1;
  if (gv->fp_kind != TK_FLOAT_KIND_NONE) {
    addr->pointee_fp_kind = (unsigned int)gv->fp_kind;
  } else if (gv->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    addr->pointee_fp_kind = (unsigned int)gv->pointee_fp_kind;
    addr->base_deref_size = 8;
  }
  addr->pointee_is_bool = gvar_pointee_is_bool(gv);
  addr->pointee_is_unsigned = gvar_pointee_is_unsigned(gv);
  psx_node_copy_funcptr_metadata_from_gvar(addr, gv);
  if (gv->pointee_elem_size > 0 && gv->tag_kind == TK_EOF) {
    addr->pointee_is_scalar_ptr = 1;
    if (addr->base_deref_size == 0) addr->base_deref_size = (short)gv->pointee_elem_size;
  }
  if (gv->tag_kind != TK_EOF && gv->is_tag_pointer) {
    if (addr->base_deref_size == 0) addr->base_deref_size = (short)gv->deref_size;
    if (addr->pointer_qual_levels == 0) addr->pointer_qual_levels = 1;
  }
  apply_array_addr_decl_type(addr, array_type);
}

void psx_node_init_compound_lvar_array_addr_metadata(node_mem_t *addr, const lvar_t *var,
                                                     token_kind_t tag_kind, char *tag_name,
                                                     int tag_len, int array_size) {
  if (!addr || !var) return;
  psx_type_t *array_type = psx_lvar_get_decl_type((lvar_t *)var);
  addr->tag_kind = tag_kind;
  addr->tag_name = tag_name;
  addr->tag_len = tag_len;
  init_lvar_array_addr_strides(addr, var);
  addr->is_pointer = 1;
  if (addr->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
  addr->compound_literal_array_size = array_size;
  apply_array_addr_decl_type(addr, array_type);
}

void psx_node_init_compound_gvar_array_addr_metadata(node_mem_t *addr, const global_var_t *gv,
                                                     int ptr_array_pointee_bytes,
                                                     int pointer_elem_size, int array_size) {
  if (!addr || !gv) return;
  psx_type_t *array_type = psx_gvar_get_decl_type((global_var_t *)gv);
  addr->tag_kind = gv->tag_kind;
  addr->tag_name = gv->tag_name;
  addr->tag_len = gv->tag_len;
  init_gvar_array_addr_strides(addr, gv);
  if (ptr_array_pointee_bytes > 0) {
    addr->ptr_array_pointee_bytes = ptr_array_pointee_bytes;
    addr->base_deref_size = (short)(pointer_elem_size > 0 ? pointer_elem_size : 8);
  }
  addr->is_pointer = 1;
  if (gv->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
  addr->compound_literal_array_size = array_size;
  apply_array_addr_decl_type(addr, array_type);
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

  int sz = 0;
  if (node->type) sz = psx_type_sizeof(psx_node_get_type(node));
  node_mem_t *mem = node_mem_view(node);
  if (sz <= 0 && mem && mem->type_size > 0) sz = mem->type_size;
  if (sz <= 0) sz = ps_node_type_size(node);

  int zext = mem && mem->widen_zext_i64 ? 1 : 0;
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
  if (node->type) {
    if (type_is_pointer_view_type(type)) return (type->pointer_const_qual_mask & 1u) ? 1 : 0;
    return type && type->is_const_qualified ? 1 : 0;
  }
  node_mem_t *mem = node_mem_view(node);
  if (!mem) return 0;
  if (mem->is_pointer || mem->is_tag_pointer)
    return (mem->pointer_const_qual_mask & 1u) ? 1 : 0;
  return mem->is_const_qualified ? 1 : 0;
}

static int node_self_is_volatile_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (node->type) {
    if (type_is_pointer_view_type(type))
      return (type->pointer_volatile_qual_mask & 1u) ? 1 : 0;
    return type && type->is_volatile_qualified ? 1 : 0;
  }
  node_mem_t *mem = node_mem_view(node);
  if (!mem) return 0;
  if (mem->is_pointer || mem->is_tag_pointer)
    return (mem->pointer_volatile_qual_mask & 1u) ? 1 : 0;
  return mem->is_volatile_qualified ? 1 : 0;
}

static int node_is_array_view(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type) return type->kind == PSX_TYPE_ARRAY;
  node_mem_t *mem = node_mem_view(node);
  return mem && mem->is_pointer && !mem->is_tag_pointer &&
         !mem->is_scalar_ptr_member && (mem->is_array_member || mem->type_size > 8) &&
         mem->deref_size > 0;
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

static int node_pointer_stride_from_type(const psx_type_t *type, int *inner_stride,
                                         int *next_stride, int *extra_strides,
                                         int *extra_strides_count) {
  if (!type || !type_is_pointer_view_type(type)) return 0;
  int count = type->extra_strides_count;
  if (count < 0) count = 0;
  if (count > 5) count = 5;
  if (type->kind == PSX_TYPE_ARRAY && type->is_vla &&
      type->vla_row_stride_frame_off == 0 && type->deref_size > 0 &&
      type->outer_stride > type->deref_size && type->mid_stride <= 0 &&
      count <= 0) {
    if (inner_stride) *inner_stride = type->deref_size;
    if (next_stride) *next_stride = 0;
    if (extra_strides_count) *extra_strides_count = 0;
    if (extra_strides) {
      for (int i = 0; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->vla_row_stride_frame_off != 0 &&
      type->outer_stride <= 0 && type->mid_stride <= 0 && count <= 0) {
    int inner = type->base_deref_size > 0 ? type->base_deref_size
                                          : psx_type_deref_size(type->base);
    if (inner <= 0) inner = type->deref_size;
    if (inner <= 0) return 0;
    if (inner_stride) *inner_stride = inner;
    if (next_stride) *next_stride = type->vla_strides_remaining > 0 ? inner : 0;
    if (extra_strides_count) *extra_strides_count = 0;
    if (extra_strides) {
      for (int i = 0; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->ptr_array_pointee_bytes <= 0 &&
      type->vla_row_stride_frame_off == 0 && type->deref_size > 0 &&
      type->outer_stride > type->deref_size && type->mid_stride <= 0 &&
      count <= 0) {
    if (inner_stride) *inner_stride = type->deref_size;
    if (next_stride) *next_stride = 0;
    if (extra_strides_count) *extra_strides_count = 0;
    if (extra_strides) {
      for (int i = 0; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_ARRAY && type->ptr_array_pointee_bytes <= 0) {
    int base_size = psx_type_sizeof(type->base);
    if (base_size > 0 && type->outer_stride >= base_size) {
      int inner = type->mid_stride > 0 ? type->mid_stride
                                       : psx_type_deref_size(type->base);
      if (inner <= 0) inner = psx_type_deref_size(type);
      if (inner <= 0) return 0;
      int next = 0;
      int shifted_count = 0;
      if (count > 0) {
        next = type->extra_strides[0];
        shifted_count = count - 1;
      }
      if (next <= 0 && type->mid_stride > 0) {
        next = psx_type_deref_size(type->base);
      }
      if (inner_stride) *inner_stride = inner;
      if (next_stride) *next_stride = next;
      if (extra_strides_count) *extra_strides_count = shifted_count;
      if (extra_strides) {
        for (int i = 0; i < shifted_count && i < 5; i++)
          extra_strides[i] = type->extra_strides[i + 1];
        for (int i = shifted_count; i < 5; i++) extra_strides[i] = 0;
      }
      return 1;
    }
  }
  if (type->kind == PSX_TYPE_POINTER && type->ptr_array_pointee_bytes <= 0 &&
      type->deref_size > 0 && type->outer_stride > 0 &&
      type->deref_size > type->outer_stride) {
    if (inner_stride) *inner_stride = type->outer_stride;
    if (next_stride) *next_stride = type->mid_stride;
    if (extra_strides_count) *extra_strides_count = count;
    if (extra_strides) {
      for (int i = 0; i < count; i++) extra_strides[i] = type->extra_strides[i];
      for (int i = count; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->ptr_array_pointee_bytes <= 0 &&
      type->outer_stride > 0 && type->mid_stride > 0) {
    int inner = type->mid_stride;
    int next = 0;
    int shifted_count = 0;
    if (count > 0) {
      next = type->extra_strides[0];
      shifted_count = count - 1;
    }
    if (next <= 0) {
      if (type->deref_size > 0 && type->deref_size < inner) {
        next = type->deref_size;
      } else {
        next = type->base_deref_size > 0 ? type->base_deref_size
                                         : psx_type_sizeof(type->base);
      }
    }
    if (inner_stride) *inner_stride = inner;
    if (next_stride) *next_stride = next;
    if (extra_strides_count) *extra_strides_count = shifted_count;
    if (extra_strides) {
      for (int i = 0; i < shifted_count && i < 5; i++)
        extra_strides[i] = type->extra_strides[i + 1];
      for (int i = shifted_count; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->ptr_array_pointee_bytes > 0 && type->base_deref_size > 0 &&
      type->vla_row_stride_frame_off == 0 && type->mid_stride > 0) {
    int inner = type->mid_stride;
    if (type->kind == PSX_TYPE_POINTER && type->base &&
        type->base->kind == PSX_TYPE_ARRAY) {
      int array_elem_stride = type_array_outer_element_size(type->base);
      if (array_elem_stride > 0) inner = array_elem_stride;
    }
    int next = 0;
    int shifted_count = 0;
    if (count > 0) {
      next = type->extra_strides[0];
      shifted_count = count - 1;
    }
    if (next <= 0) {
      if (type->deref_size > 0 && type->deref_size < inner) {
        next = type->deref_size;
      } else {
        next = type->base_deref_size;
      }
    }
    if (inner_stride) *inner_stride = inner;
    if (next_stride) *next_stride = next;
    if (extra_strides_count) *extra_strides_count = shifted_count;
    if (extra_strides) {
      for (int i = 0; i < shifted_count && i < 5; i++)
        extra_strides[i] = type->extra_strides[i + 1];
      for (int i = shifted_count; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->ptr_array_pointee_bytes > 0 && type->base_deref_size > 0 &&
      type->outer_stride >= type->ptr_array_pointee_bytes &&
      type->mid_stride <= 0 && count <= 0) {
    int elem_stride = type->base_deref_size;
    if (type->base && type->base->kind == PSX_TYPE_POINTER) {
      int pointer_elem_size = psx_type_sizeof(type->base);
      if (pointer_elem_size > 0) elem_stride = pointer_elem_size;
    } else if (type->base && type->base->kind == PSX_TYPE_ARRAY) {
      int array_elem_stride = type_array_outer_element_size(type->base);
      if (array_elem_stride > 0) elem_stride = array_elem_stride;
    }
    if (inner_stride) *inner_stride = elem_stride;
    if (next_stride) *next_stride = 0;
    if (extra_strides_count) *extra_strides_count = 0;
    if (extra_strides) {
      for (int i = 0; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_ARRAY && type->base &&
      type->base->kind == PSX_TYPE_ARRAY &&
      type->outer_stride > 0 && type->mid_stride <= 0 && count <= 0) {
    int next = psx_type_deref_size(type->base);
    if (inner_stride) *inner_stride = type->outer_stride;
    if (next_stride) *next_stride = next > 0 ? next : 0;
    if (extra_strides_count) *extra_strides_count = 0;
    if (extra_strides) {
      for (int i = 0; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->outer_stride <= 0 && type->mid_stride <= 0 && count <= 0) return 0;
  if (inner_stride) *inner_stride = type->outer_stride;
  if (next_stride) *next_stride = type->mid_stride;
  if (extra_strides_count) *extra_strides_count = count;
  if (extra_strides) {
    for (int i = 0; i < count; i++) extra_strides[i] = type->extra_strides[i];
    for (int i = count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
}

static int node_pointer_stride_from_mem(const node_mem_t *mem, int *inner_stride,
                                        int *next_stride, int *extra_strides,
                                        int *extra_strides_count) {
  if (!mem) return 0;
  int count = mem->extra_strides_count;
  if (count < 0) count = 0;
  if (count > 5) count = 5;
  if (mem->inner_deref_size <= 0 && mem->next_deref_size <= 0 && count <= 0) return 0;
  if (inner_stride) *inner_stride = mem->inner_deref_size;
  if (next_stride) *next_stride = mem->next_deref_size;
  if (extra_strides_count) *extra_strides_count = count;
  if (extra_strides) {
    for (int i = 0; i < count; i++) extra_strides[i] = mem->extra_strides[i];
    for (int i = count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
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
  int had_direct_type = node->type != NULL;
  psx_type_t *type = psx_node_get_type(node);
  if (node->kind == ND_DEREF && type && type->kind == PSX_TYPE_ARRAY &&
      node_pointer_stride_from_mem(node_mem_view(node), inner_stride, next_stride,
                                   extra_strides, extra_strides_count)) {
    return 1;
  }
  if (node_pointer_stride_from_type(type, inner_stride, next_stride,
                                    extra_strides, extra_strides_count)) {
    return 1;
  }
  if (node_pointer_stride_from_funcall_return(node, type, inner_stride, next_stride))
    return 1;
  if (had_direct_type) {
    if (type && type->kind == PSX_TYPE_ARRAY && type->is_vla) {
      return node_pointer_stride_from_mem(node_mem_view(node), inner_stride, next_stride,
                                          extra_strides, extra_strides_count);
    }
    return 0;
  }
  return node_pointer_stride_from_mem(node_mem_view(node), inner_stride, next_stride,
                                      extra_strides, extra_strides_count);
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
  if (node->type) return TK_FLOAT_KIND_NONE;
  return (tk_float_kind_t)node->fp_kind;
}

int psx_node_value_is_complex(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && !psx_type_is_pointer(type)) return type->kind == PSX_TYPE_COMPLEX;
  if (node->type) return 0;
  node_mem_t *mem = node_mem_view(node);
  if (mem) return mem->is_complex ? 1 : 0;
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
  int u = is_unsigned ? 1 : 0;
  switch (node->kind) {
    case ND_LVAR: as_lvar(node)->mem.is_unsigned = u; break;
    case ND_GVAR:
    case ND_DEREF:
    case ND_CAST:
    case ND_ASSIGN:
      as_mem(node)->is_unsigned = u; break;
    default: node->is_unsigned = u; break;
  }
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

node_t *psx_node_new_lvar(int offset) {
  node_lvar_t *node = arena_alloc(sizeof(node_lvar_t));
  node->mem.base.kind = ND_LVAR;
  node->mem.tag_kind = TK_EOF;
  node->offset = offset;
  node->mem.type_size = 8;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_typed(int offset, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar(offset);
  node->mem.type_size = type_size;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_typed_at_for(lvar_t *owner, int offset, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(offset, type_size);
  node->var = owner;
  if (owner) {
    psx_type_t *owner_type = psx_lvar_get_decl_type(owner);
    int rel = offset - owner->offset;
    if (rel == 0 && psx_type_sizeof(owner_type) == type_size) {
      node->mem.base.type = owner_type;
    } else if (rel >= 0 && owner->elem_size > 0 &&
               (rel % owner->elem_size) == 0) {
      node->mem.base.type =
          type_array_element_type_for_size(owner_type, type_size);
    }
    sync_scalar_mem_from_decl_type(&node->mem, node->mem.base.type);
    sync_pointer_cast_mem_from_type(&node->mem, node->mem.base.type);
  }
  return (node_t *)node;
}

node_t *psx_node_new_lvar_scalar_slot_at(int offset, int type_size,
                                         tk_float_kind_t fp_kind, int is_bool) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(offset, type_size);
  node->mem.base.fp_kind = fp_kind;
  node->mem.is_bool = is_bool ? 1 : 0;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_fp_slot_at(int offset, int type_size, tk_float_kind_t fp_kind) {
  return psx_node_new_lvar_scalar_slot_at(offset, type_size, fp_kind, 0);
}

node_t *psx_node_new_lvar_fp_slot_for(lvar_t *owner, int offset, int type_size) {
  tk_float_kind_t fp_kind = psx_lvar_fp_kind(owner);
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_fp_slot_at(
      offset, type_size, fp_kind);
  node->var = owner;
  if (fp_kind != TK_FLOAT_KIND_NONE)
    node->mem.base.type = psx_type_new_float(fp_kind, type_size);
  return (node_t *)node;
}

node_t *psx_node_new_param_placeholder(int is_pointer, tk_float_kind_t fp_kind, int is_unsigned) {
  if (is_pointer) {
    node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(0, 8);
    node->mem.is_pointer = 1;
    node->mem.base.is_unsigned = is_unsigned ? 1 : 0;
    node->mem.is_unsigned = is_unsigned ? 1 : 0;
    return (node_t *)node;
  }
  node_t *node = psx_node_new_num(0);
  node->fp_kind = fp_kind;
  node->is_unsigned = is_unsigned ? 1 : 0;
  return node;
}

node_t *psx_node_new_unsigned_lvar_typed(int offset, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(offset, type_size);
  node->mem.base.is_unsigned = 1;
  node->mem.is_unsigned = 1;
  return (node_t *)node;
}

static void sync_lvar_ref_mem_from_decl_type(node_mem_t *mem,
                                             psx_type_t *decl_type) {
  if (!mem || !decl_type) return;
  mem->base.type = decl_type;
  int type_size = psx_type_sizeof(decl_type);
  if (type_size > 0) mem->type_size = (short)type_size;
  mem->is_pointer = psx_type_is_pointer(decl_type) ? 1 : 0;
  sync_scalar_mem_from_decl_type(mem, decl_type);
  sync_pointer_cast_mem_from_type(mem, decl_type);
  if (psx_decl_funcptr_sig_has_payload(decl_type->funcptr_sig))
    node_mem_store_funcptr_signature(mem, &decl_type->funcptr_sig);
  sync_tag_mem_from_decl_type(mem, decl_type);
}

node_t *psx_node_new_lvar_for(lvar_t *var) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar(var ? var->offset : 0);
  if (var) {
    init_lvar_ref_mem_from_var(&node->mem, var);
    sync_lvar_ref_mem_from_decl_type(&node->mem, psx_lvar_get_decl_type(var));
  }
  node->var = var;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_typed_for(lvar_t *var, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_for(var);
  node->mem.type_size = type_size;
  return (node_t *)node;
}

static int lvar_public_storage_size_or_elem(const lvar_t *var) {
  int elem_size = psx_lvar_elem_size(var, 0);
  return psx_lvar_storage_size(var, elem_size);
}

node_t *psx_node_new_lvar_object_ref_for(lvar_t *var) {
  return psx_node_new_lvar_typed_for(var, lvar_public_storage_size_or_elem(var));
}

node_t *psx_node_new_lvar_expr_ref_for(lvar_t *var, int is_pointer) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed_for(
      var, is_pointer ? 8 : (var ? var->elem_size : 0));
  if (var) node->mem.deref_size = var->elem_size;
  node->mem.is_pointer = is_pointer ? 1 : 0;
  psx_type_t *decl_type = var ? psx_lvar_get_decl_type(var) : NULL;
  if (decl_type) {
    sync_lvar_identifier_mem_from_decl_type(
        node, var, decl_type, psx_type_is_pointer(decl_type) ? 1 : 0);
  }
  return (node_t *)node;
}

static int lvar_is_identifier_pointer_like(const lvar_t *var) {
  if (!var) return 0;
  return var->is_array || var->is_vla || var->pointer_qual_levels > 0 ||
         (var->size > var->elem_size) ||
         (var->outer_stride > 0 && var->size == 8 && !var->is_array && !var->is_vla) ||
         var->is_tag_pointer ||
         var->pointee_fp_kind != TK_FLOAT_KIND_NONE;
}

static int lvar_identifier_is_pointer_like(lvar_t *var, psx_type_t *decl_type) {
  if (decl_type) return psx_type_is_pointer(decl_type);
  return lvar_is_identifier_pointer_like(var);
}

static int lvar_identifier_deref_size_from_type(const lvar_t *var,
                                                const psx_type_t *decl_type) {
  (void)var;
  if (!decl_type || !psx_type_is_pointer(decl_type)) return 0;
  if (decl_type->kind == PSX_TYPE_POINTER &&
      decl_type->vla_row_stride_frame_off != 0 &&
      decl_type->outer_stride <= 0) {
    return 0;
  }
  if (decl_type->kind == PSX_TYPE_POINTER &&
      decl_type->ptr_array_pointee_bytes > 0) {
    int base_size = decl_type->base ? psx_type_sizeof(decl_type->base) : 0;
    if (decl_type->ptr_array_pointee_bytes > 0 &&
        base_size > 0 && decl_type->base_deref_size > 0 &&
        base_size > decl_type->base_deref_size) {
      return base_size;
    }
    return decl_type->ptr_array_pointee_bytes;
  }
  if (decl_type->kind == PSX_TYPE_POINTER && decl_type->outer_stride > 0) {
    return decl_type->outer_stride;
  }
  if (decl_type->kind == PSX_TYPE_POINTER && decl_type->base &&
      decl_type->base->kind == PSX_TYPE_ARRAY) {
    int base_size = psx_type_sizeof(decl_type->base);
    if (base_size > 0) return base_size;
    if (decl_type->outer_stride > 0) return decl_type->outer_stride;
  }
  if (decl_type->kind == PSX_TYPE_ARRAY && decl_type->outer_stride > 0) {
    return decl_type->outer_stride;
  }
  return psx_type_deref_size(decl_type);
}

static void sync_lvar_identifier_mem_from_decl_type(node_lvar_t *node,
                                                    const lvar_t *var,
                                                    psx_type_t *decl_type,
                                                    int is_pointer) {
  if (!node || !decl_type) return;
  node->mem.base.type = decl_type;
  int type_size = is_pointer ? 8 : psx_type_sizeof(decl_type);
  if (type_size > 0) node->mem.type_size = (short)type_size;
  node->mem.is_pointer = is_pointer ? 1 : 0;
  node->mem.pointee_fp_kind =
      (unsigned int)type_deep_pointee_fp_kind(decl_type);
  sync_pointee_flags_mem_from_type(&node->mem, decl_type);
  sync_scalar_mem_from_decl_type(&node->mem, decl_type);
  sync_pointer_cast_mem_from_type(&node->mem, decl_type);
  if (!is_pointer) return;

  int deref_size = lvar_identifier_deref_size_from_type(var, decl_type);
  node->mem.deref_size = (short)deref_size;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (node_pointer_stride_from_type(decl_type, &inner_stride, &next_stride,
                                    extra_strides, &extra_count)) {
    node->mem.inner_deref_size = (short)inner_stride;
    node->mem.next_deref_size = (short)next_stride;
    node->mem.extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      node->mem.extra_strides[i] = extra_strides[i];
    for (int i = extra_count; i < 5; i++)
      node->mem.extra_strides[i] = 0;
  }
  int base_deref_size = type_pointer_view_base_deref_size(decl_type, 1);
  if (base_deref_size > 0) node->mem.base_deref_size = (short)base_deref_size;
  node->mem.pointer_qual_levels =
      (unsigned char)(decl_type->pointer_qual_levels > 0
                          ? decl_type->pointer_qual_levels
                          : 1);
  node->mem.ptr_array_pointee_bytes =
      decl_type->ptr_array_pointee_bytes > 0
          ? decl_type->ptr_array_pointee_bytes
          : 0;
  node->mem.vla_row_stride_frame_off = decl_type->vla_row_stride_frame_off;
  node->mem.vla_strides_remaining = decl_type->vla_strides_remaining;
}

node_t *psx_node_new_lvar_identifier_ref_for(lvar_t *var) {
  if (var && var->is_static_local && var->static_global_name) {
    int sz = var->size > 0 ? var->size : var->elem_size;
    return psx_node_new_static_local_gvar_for(var, sz);
  }

  psx_type_t *decl_type = var ? psx_lvar_get_decl_type(var) : NULL;
  int is_pointer = lvar_identifier_is_pointer_like(var, decl_type);
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed_for(
      var, is_pointer ? 8 : (var ? var->elem_size : 0));

  int effective_deref = 0;
  if (var && is_pointer) {
    effective_deref = (var->outer_stride > 0) ? var->outer_stride
                      : (var->vla_row_stride_frame_off ? 0 : var->elem_size);
  }
  node->mem.deref_size = (short)effective_deref;

  if (var) {
    int is_multidim = (var->outer_stride != var->elem_size) ||
                      (var->vla_row_stride_frame_off != 0);
    if (var->mid_stride > 0) {
      node->mem.inner_deref_size = (short)var->mid_stride;
      node->mem.next_deref_size = (short)var->elem_size;
    } else if (var->vla_strides_remaining > 0) {
      node->mem.inner_deref_size = (short)var->elem_size;
      node->mem.next_deref_size = (short)var->elem_size;
    } else {
      node->mem.inner_deref_size = (short)(is_multidim ? var->elem_size : 0);
    }
  }
  node->mem.is_pointer = is_pointer ? 1 : 0;
  sync_lvar_identifier_mem_from_decl_type(node, var, decl_type, is_pointer);
  return (node_t *)node;
}

node_t *psx_node_new_param_lvar_for(lvar_t *var, int abi_type_size,
                                    int is_unsigned, tk_float_kind_t abi_fp_kind,
                                    int is_complex) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed_for(var, abi_type_size);

  psx_type_t *decl_type = var ? psx_lvar_get_decl_type(var) : NULL;
  if (decl_type) {
    node->mem.base.type = decl_type;
    sync_scalar_mem_from_decl_type(&node->mem, decl_type);
    sync_pointer_cast_mem_from_type(&node->mem, decl_type);
    if (!lvar_identifier_is_pointer_like(var, decl_type)) {
      int canonical_type_size = psx_type_sizeof(decl_type);
      if (canonical_type_size > 0)
        node->mem.type_size = (short)canonical_type_size;
    }
  } else {
    node->mem.base.is_unsigned = is_unsigned ? 1 : 0;
    node->mem.is_unsigned = is_unsigned ? 1 : 0;
    if (abi_fp_kind != TK_FLOAT_KIND_NONE) node->mem.base.fp_kind = abi_fp_kind;
    if (is_complex) {
      node->mem.base.is_complex = 1;
      node->mem.is_complex = 1;
    }
  }

  if (lvar_identifier_is_pointer_like(var, decl_type)) {
    node->mem.is_pointer = 1;
    node->mem.type_size = 8;
    node->mem.deref_size = (var->outer_stride > 0) ? (short)var->outer_stride
                           : (var->vla_row_stride_frame_off ? 0 : (short)var->elem_size);
    sync_lvar_identifier_mem_from_decl_type(node, var, decl_type, 1);
    node->mem.type_size = 8;
  }
  return (node_t *)node;
}

node_t *psx_node_new_array_elem_lvar_for(lvar_t *var, int idx) {
  int elem_size = var ? var->elem_size : 0;
  int offset = var ? var->offset + idx * elem_size : 0;
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(offset, elem_size);
  node->var = var;
  if (var) {
    node->mem.base.type = type_array_leaf_element_type(psx_lvar_get_decl_type(var));
    node->mem.base.fp_kind = var->fp_kind;
    node->mem.base.is_unsigned = var->is_unsigned ? 1 : 0;
    node->mem.is_unsigned = var->is_unsigned ? 1 : 0;
    node->mem.is_bool = var->is_bool ? 1 : 0;
    node->mem.tag_kind = var->tag_kind;
    node->mem.tag_name = var->tag_name;
    node->mem.tag_len = var->tag_len;
    node->mem.tag_scope_depth_p1 = var->tag_scope_depth_p1;
    node->mem.is_tag_pointer = var->is_tag_pointer ? 1 : 0;
    sync_scalar_mem_from_decl_type(&node->mem, node->mem.base.type);
    sync_pointer_cast_mem_from_type(&node->mem, node->mem.base.type);
  }
  return (node_t *)node;
}

static node_t *annotate_explicit_type(node_t *node, psx_type_t *type) {
  if (node && type) node->type = type;
  return node;
}

node_t *psx_node_new_fp_to_int_cast(node_t *operand, int width, psx_type_t *cast_type) {
  node_mem_t *mem = arena_alloc(sizeof(node_mem_t));
  mem->base.kind = ND_FP_TO_INT;
  mem->base.lhs = operand;
  mem->base.fp_kind = TK_FLOAT_KIND_NONE;
  mem->type_size = (short)((width == 8) ? 8 : 4);
  if (!cast_type) cast_type = psx_type_new_integer(TK_INT, mem->type_size, 0);
  sync_scalar_mem_from_decl_type(mem, cast_type);
  return annotate_explicit_type((node_t *)mem, cast_type);
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
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  wrap->type_size = (short)type_size;
  wrap->is_unsigned = is_unsigned ? 1 : 0;
  wrap->is_long_long = is_long_long ? 1 : 0;
  wrap->is_plain_char = is_plain_char ? 1 : 0;
  wrap->widen_zext_i64 = widen_zext_i64 ? 1 : 0;
  sync_scalar_mem_from_decl_type(wrap, cast_type);
  return annotate_explicit_type((node_t *)wrap, cast_type);
}

node_t *psx_node_new_i64_to_i32_trunc_cast(node_t *operand, psx_type_t *cast_type,
                                           int is_unsigned) {
  node_t *trunc = psx_node_new_shift_trunc_extend(operand, 32, is_unsigned);
  return psx_node_new_integer_cast_result(trunc, cast_type, 4, is_unsigned, 0);
}

static void sync_pointer_cast_mem_from_type(node_mem_t *mem, psx_type_t *type) {
  if (!mem || !type_is_pointer_view_type(type)) return;
  clear_pointer_payload_mem(mem);
  mem->is_pointer = 1;
  int type_size = psx_type_sizeof(type);
  if (type_size > 0) mem->type_size = (short)type_size;
  int deref_size = psx_type_deref_size(type);
  if (deref_size > 0) mem->deref_size = (short)deref_size;
  int base_deref_size = type_pointer_view_base_deref_size(type, 1);
  if (base_deref_size > 0) mem->base_deref_size = (short)base_deref_size;

  int pointer_levels = 0;
  if (pointer_view_from_type(type, NODE_POINTER_QUAL_LEVELS, &pointer_levels) &&
      pointer_levels > 0) {
    mem->pointer_qual_levels = (unsigned char)pointer_levels;
  }
  mem->pointer_const_qual_mask = type->pointer_const_qual_mask;
  mem->pointer_volatile_qual_mask = type->pointer_volatile_qual_mask;
  mem->is_pointer_const_qualified =
      (type->pointer_const_qual_mask & 1u) ? 1 : 0;
  mem->is_pointer_volatile_qualified =
      (type->pointer_volatile_qual_mask & 1u) ? 1 : 0;
  if (type->ptr_array_pointee_bytes > 0)
    mem->ptr_array_pointee_bytes = type->ptr_array_pointee_bytes;
  mem->vla_row_stride_frame_off = type->vla_row_stride_frame_off;
  mem->vla_strides_remaining = type->vla_strides_remaining;

  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (node_pointer_stride_from_type(type, &inner_stride, &next_stride,
                                    extra_strides, &extra_count)) {
    mem->inner_deref_size = (short)inner_stride;
    mem->next_deref_size = (short)next_stride;
    mem->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      mem->extra_strides[i] = extra_strides[i];
    for (int i = extra_count; i < 5; i++) mem->extra_strides[i] = 0;
  }

  mem->pointee_fp_kind = (unsigned int)type_deep_pointee_fp_kind(type);
  sync_pointee_flags_mem_from_type(mem, type);
  sync_tag_mem_from_decl_type(mem, type);
}

node_t *psx_node_new_pointer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         token_kind_t type_kind,
                                         token_kind_t tag_kind,
                                         char *tag_name, int tag_len,
                                         int elem_size, int is_unsigned) {
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  wrap->is_pointer = 1;
  wrap->type_size = 8;
  int pointer_levels = 1;
  if (cast_type && cast_type->kind == PSX_TYPE_POINTER) {
    pointer_levels = cast_type->pointer_qual_levels > 0 ? cast_type->pointer_qual_levels : 1;
    wrap->deref_size = (short)(cast_type->deref_size > 0 ? cast_type->deref_size : 8);
    wrap->base_deref_size = (short)(cast_type->base_deref_size > 0
                                      ? cast_type->base_deref_size
                                      : (elem_size > 0 ? elem_size : 8));
  } else {
    if (elem_size > 0) wrap->deref_size = (short)elem_size;
    wrap->base_deref_size = (short)(elem_size > 0 ? elem_size : 8);
  }
  wrap->pointer_qual_levels = (unsigned char)pointer_levels;
  int has_pointer_cast_type = type_is_pointer_view_type(cast_type);
  sync_pointer_cast_mem_from_type(wrap, cast_type);
  if (has_pointer_cast_type) {
    return annotate_explicit_type((node_t *)wrap, cast_type);
  }
  if (type_kind == TK_VOID) {
    wrap->pointee_is_void = 1;
  } else if (psx_ctx_is_tag_aggregate_kind(tag_kind)) {
    wrap->tag_kind = tag_kind;
    wrap->tag_name = tag_name;
    wrap->tag_len = tag_len;
    wrap->is_tag_pointer = 1;
  } else if (type_kind == TK_FLOAT) {
    wrap->pointee_fp_kind = TK_FLOAT_KIND_FLOAT;
    if (wrap->deref_size <= 0) wrap->deref_size = 4;
  } else if (type_kind == TK_DOUBLE) {
    wrap->pointee_fp_kind = TK_FLOAT_KIND_DOUBLE;
    if (wrap->deref_size <= 0) wrap->deref_size = 8;
  } else if (is_unsigned || type_kind == TK_UNSIGNED) {
    wrap->pointee_is_unsigned = 1;
  } else if (type_kind == TK_BOOL) {
    wrap->pointee_is_bool = 1;
  }
  return annotate_explicit_type((node_t *)wrap, cast_type);
}

node_t *psx_node_new_aggregate_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  if (cast_type) {
    int size = psx_type_sizeof(cast_type);
    if (size > 0) wrap->type_size = (short)size;
    sync_tag_mem_from_decl_type(wrap, cast_type);
  }
  return annotate_explicit_type((node_t *)wrap, cast_type);
}

node_t *psx_node_new_void_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  wrap->type_size = 0;
  return annotate_explicit_type((node_t *)wrap, cast_type);
}

static node_mem_t *new_addr_node(node_t *base) {
  node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
  addr->base.kind = ND_ADDR;
  addr->base.lhs = base;
  return addr;
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
  ptr->ptr_array_pointee_bytes = array_type->ptr_array_pointee_bytes;
  ptr->outer_stride = array_type->outer_stride;
  ptr->mid_stride = array_type->mid_stride;
  ptr->extra_strides_count = array_type->extra_strides_count;
  for (int i = 0; i < 5; i++) ptr->extra_strides[i] = array_type->extra_strides[i];
  ptr->vla_row_stride_frame_off = array_type->vla_row_stride_frame_off;
  ptr->vla_strides_remaining = array_type->vla_strides_remaining;
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
  if (type->kind == PSX_TYPE_ARRAY && type->pointer_qual_levels <= 0) {
    return type->base;
  }
  if (type->kind == PSX_TYPE_POINTER && type->base->kind == PSX_TYPE_ARRAY &&
      type->pointer_qual_levels <= 1) {
    return type->base;
  }
  if ((!type_is_pointer_view_type(type->base) || type->ptr_array_pointee_bytes > 0) &&
      type->pointer_qual_levels <= 1) {
    int elem_size = type->base_deref_size > 0 ? type->base_deref_size
                                              : psx_type_sizeof(type->base);
    if (type->ptr_array_pointee_bytes > 0 && type_is_pointer_view_type(type->base)) {
      int pointer_elem_size = psx_type_sizeof(type->base);
      if (pointer_elem_size > 0) elem_size = pointer_elem_size;
    }
    int is_array_view = (type->kind == PSX_TYPE_ARRAY &&
                         type->pointer_qual_levels == 0) ||
                        type->ptr_array_pointee_bytes > 0 ||
                        type->mid_stride > 0 ||
                        type->extra_strides_count > 0;
    int array_size = type->deref_size;
    if (type->outer_stride > array_size) array_size = type->outer_stride;
    if (type->ptr_array_pointee_bytes > array_size)
      array_size = type->ptr_array_pointee_bytes;
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
              ? type->ptr_array_pointee_bytes : 0;
      array->outer_stride = type->outer_stride;
      array->mid_stride = type->mid_stride;
      array->extra_strides_count = type->extra_strides_count;
      for (int i = 0; i < 5; i++) array->extra_strides[i] = type->extra_strides[i];
      return array;
    }
  }
  if (!type_is_pointer_view_type(type->base) && type->pointer_qual_levels >= 2) {
    int deref_size = type->base_deref_size > 0 ? type->base_deref_size
                                               : psx_type_sizeof(type->base);
    if (deref_size <= 0) deref_size = type->deref_size;
    if (deref_size <= 0) deref_size = 8;
    psx_type_t *result = psx_type_new_pointer(type->base, deref_size);
    result->pointer_qual_levels = type->pointer_qual_levels - 1;
    result->base_deref_size = deref_size;
    result->pointer_const_qual_mask = type->pointer_const_qual_mask >> 1;
    result->pointer_volatile_qual_mask = type->pointer_volatile_qual_mask >> 1;
    result->pointee_fp_kind = type->pointee_fp_kind;
    result->funcptr_sig =
        psx_decl_funcptr_sig_clone(funcptr_sig_for_deref_result(
            type->funcptr_sig, type->base, result->pointer_qual_levels));
    result->ptr_array_pointee_bytes = type->ptr_array_pointee_bytes;
    result->outer_stride = type->outer_stride;
    result->mid_stride = type->mid_stride;
    result->extra_strides_count = type->extra_strides_count;
    for (int i = 0; i < 5; i++) result->extra_strides[i] = type->extra_strides[i];
    return result;
  }
  return type->base;
}

static void sync_unary_deref_mem_from_pointer_type(node_mem_t *node) {
  if (!node || !node->base.type || node->base.type->kind != PSX_TYPE_POINTER) return;
  psx_type_t *type = node->base.type;
  node->is_pointer = 1;
  node->type_size = 8;
  int pql = type->pointer_qual_levels > 0 ? type->pointer_qual_levels : 1;
  int bds = type->base_deref_size > 0 ? type->base_deref_size : psx_type_deref_size(type);
  int type_deref_size = psx_type_deref_size(type);
  int deref_size = (pql <= 1 && bds > 0 && type_deref_size <= 8)
                       ? bds
                       : type_deref_size;
  if (deref_size <= 0) deref_size = type->deref_size;
  if (deref_size <= 0) deref_size = 8;
  node->deref_size = (short)deref_size;
  node->pointer_qual_levels = (unsigned char)pql;
  if (bds <= 0) bds = deref_size;
  node->base_deref_size = (short)bds;
  node->pointer_const_qual_mask = type->pointer_const_qual_mask;
  node->pointer_volatile_qual_mask = type->pointer_volatile_qual_mask;
  node->pointee_fp_kind = type->pointee_fp_kind;
  node->funcptr_sig = psx_decl_funcptr_sig_clone(type->funcptr_sig);
  node->ptr_array_pointee_bytes = type->ptr_array_pointee_bytes > 0
                                      ? type->ptr_array_pointee_bytes
                                      : 0;
  node->inner_deref_size = (short)type->outer_stride;
  node->next_deref_size = (short)type->mid_stride;
  node->extra_strides_count = type->extra_strides_count;
  for (int i = 0; i < 5; i++) node->extra_strides[i] = type->extra_strides[i];
}

static int sync_unary_deref_mem_from_scalar_type(node_mem_t *node,
                                                 psx_type_t *type) {
  if (!node || !type) return 0;
  if (type->kind != PSX_TYPE_BOOL &&
      type->kind != PSX_TYPE_INTEGER &&
      type->kind != PSX_TYPE_FLOAT &&
      type->kind != PSX_TYPE_COMPLEX) {
    return 0;
  }
  node->type_size = psx_type_sizeof(type);
  node->deref_size = 0;
  node->is_pointer = 0;
  node->is_tag_pointer = 0;
  node->pointer_qual_levels = 0;
  node->base_deref_size = 0;
  node->ptr_array_pointee_bytes = 0;
  sync_scalar_mem_from_decl_type(node, type);
  return 1;
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
      (!base_type->base->base || base_type->base->base->kind != PSX_TYPE_ARRAY)) {
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
      base_elem_size > 0 && inner_deref_size == base_elem_size;
  int subscript_yields_tag_element =
      psx_type_is_tag_aggregate(view->base) &&
      base_elem_size > 0 && elem_size == base_elem_size;
  int keeps_row = inner_deref_size > 0 && elem_size > inner_deref_size &&
                  !subscript_yields_pointer_element &&
                  !subscript_yields_tag_element;
  if (!keeps_row) {
    return type_with_funcptr_sig(view->base, view->funcptr_sig);
  }

  if (view->base->kind == PSX_TYPE_ARRAY &&
      psx_type_sizeof(view->base) == elem_size) {
    return view->base;
  }

  int row_elem_size = inner_deref_size > 0 ? inner_deref_size
                                           : psx_type_sizeof(view->base);
  if (row_elem_size <= 0) row_elem_size = view->elem_size;
  if (row_elem_size <= 0) return view->base;
  int row_len = elem_size / row_elem_size;
  if (row_len <= 0) row_len = 1;
  psx_type_t *row = psx_type_new_array(view->base, row_len, elem_size,
                                       row_elem_size, view->is_vla);
  row->base_deref_size = view->base_deref_size > 0
                             ? view->base_deref_size
                             : row_elem_size;
  row->outer_stride = next_deref_size;
  row->pointee_fp_kind = view->pointee_fp_kind;
  row->funcptr_sig = psx_decl_funcptr_sig_clone(view->funcptr_sig);
  row->ptr_array_pointee_bytes = view->ptr_array_pointee_bytes;
  row->vla_row_stride_frame_off = view->vla_row_stride_frame_off;
  row->vla_strides_remaining = view->vla_strides_remaining > 0
                                   ? view->vla_strides_remaining - 1
                                   : view->vla_strides_remaining;
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
  node_mem_t *addr = new_addr_node(psx_node_new_gvar_array_base_for(gv));
  psx_node_init_gvar_array_addr_metadata(addr, gv);
  return (node_t *)addr;
}

node_t *psx_node_new_static_local_array_addr_for(lvar_t *var, int gvar_type_size) {
  node_mem_t *addr = new_addr_node(psx_node_new_static_local_gvar_for(var, gvar_type_size));
  psx_type_t *backing_type = static_local_backing_decl_type(var);
  init_lvar_array_addr_metadata_with_decl_type(addr, var, 0, backing_type);
  return (node_t *)addr;
}

node_t *psx_node_new_lvar_array_addr_for(lvar_t *var, int is_tag_pointer) {
  node_mem_t *addr = new_addr_node(psx_node_new_lvar_for(var));
  psx_node_init_lvar_array_addr_metadata(addr, var, is_tag_pointer);
  return (node_t *)addr;
}

node_t *psx_node_new_compound_gvar_array_addr_for(global_var_t *gv,
                                                  int ptr_array_pointee_bytes,
                                                  int pointer_elem_size,
                                                  int array_size,
                                                  psx_type_t *canonical_type) {
  node_mem_t *addr = new_addr_node(psx_node_new_gvar_for(gv));
  psx_node_init_compound_gvar_array_addr_metadata(addr, gv, ptr_array_pointee_bytes,
                                                  pointer_elem_size, array_size);
  apply_array_addr_decl_type(addr, canonical_type ? canonical_type
                                                  : psx_gvar_get_decl_type(gv));
  return (node_t *)addr;
}

node_t *psx_node_new_compound_lvar_array_addr_for(lvar_t *var,
                                                  token_kind_t tag_kind,
                                                  char *tag_name, int tag_len,
                                                  int array_size,
                                                  psx_type_t *canonical_type) {
  node_mem_t *addr = new_addr_node(psx_node_new_lvar_for(var));
  psx_node_init_compound_lvar_array_addr_metadata(addr, var, tag_kind, tag_name,
                                                  tag_len, array_size);
  apply_array_addr_decl_type(addr, canonical_type ? canonical_type
                                                  : psx_lvar_get_decl_type(var));
  return (node_t *)addr;
}

node_t *psx_node_new_addr_value_for(node_t *operand) {
  node_mem_t *addr = new_addr_node(operand);
  addr->base.type = type_from_address_operand(operand);
  sync_pointer_cast_mem_from_type(addr, addr->base.type);
  addr->type_size = 8;
  return (node_t *)addr;
}

node_t *psx_node_new_explicit_addr_value_for(node_t *operand) {
  if (!operand || operand->kind != ND_ADDR) return operand;
  node_mem_t *opm = (node_mem_t *)operand;
  if (opm->type_size != 8 || opm->compound_literal_array_size > 0) {
    node_mem_t *cp = arena_alloc(sizeof(node_mem_t));
    *cp = *opm;
    cp->type_size = 8;
    if (opm->compound_literal_array_size > 0) {
      int old_inner = opm->inner_deref_size;
      int old_next = opm->next_deref_size;
      int old_extras[5] = {0};
      int old_extra_count = opm->extra_strides_count;
      for (int i = 0; i < old_extra_count && i < 5; i++) old_extras[i] = opm->extra_strides[i];
      cp->inner_deref_size = opm->deref_size;
      cp->deref_size = opm->compound_literal_array_size;
      cp->next_deref_size = old_inner;
      cp->extra_strides_count = 0;
      for (int i = 0; i < 5; i++) cp->extra_strides[i] = 0;
      if (old_next > 0) {
        cp->extra_strides[0] = old_next;
        int n = 1;
        for (int i = 0; i < old_extra_count && n < 5; i++, n++) cp->extra_strides[n] = old_extras[i];
        cp->extra_strides_count = (unsigned char)n;
      }
    }
    cp->compound_literal_array_size = 0;
    cp->base.is_explicit_addr_expr = 1;
    return (node_t *)cp;
  }
  operand->is_explicit_addr_expr = 1;
  return operand;
}

node_t *psx_node_new_unary_addr_for(node_t *operand) {
  node_mem_t *node = new_addr_node(operand);
  node->base.type = type_from_address_operand(operand);
  sync_pointer_cast_mem_from_type(node, node->base.type);
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(operand, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  if (tag_kind != TK_EOF && !is_tag_ptr) {
    node->tag_kind = tag_kind;
    node->tag_name = tag_name;
    node->tag_len = tag_len;
    node->is_tag_pointer = 1;
    node->deref_size = ps_node_type_size(operand);
    node->type_size = 8;
    node->base.is_explicit_addr_expr = 1;
    return (node_t *)node;
  }
  int ts = ps_node_type_size(operand);
  if (ts > 0) {
    node->deref_size = ts;
    node->is_pointer = 1;
    node->type_size = 8;
  }
  node->base.is_explicit_addr_expr = 1;
  return (node_t *)node;
}

node_t *psx_node_new_tag_member_deref_for(node_t *addr_base, node_t *base,
                                          const tag_member_info_t *info) {
  if (!info) return NULL;
  node_t *addr = psx_node_new_binary(ND_ADD, addr_base, psx_node_new_num(info->offset));
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  int mem_size = psx_tag_member_decl_value_size(info);
  int mem_array_len = psx_tag_member_decl_array_count(info);
  int mem_storage_size = psx_tag_member_decl_storage_size(info);
  int mem_is_ptr = psx_tag_member_decl_is_pointer(info);
  int mem_deref_size = psx_tag_member_decl_deref_size(info);
  int mem_outer_stride = psx_tag_member_decl_outer_stride(info);
  int mem_mid_stride = psx_tag_member_decl_mid_stride(info);
  int mem_ptr_array_pointee_bytes =
      psx_tag_member_decl_ptr_array_pointee_bytes(info);
  int mem_pointer_qual_levels = psx_tag_member_decl_pointer_qual_levels(info);
  tk_float_kind_t mem_fp_kind = psx_tag_member_decl_fp_kind(info);
  int mem_is_bool = psx_tag_member_decl_is_bool(info);
  int mem_is_unsigned = psx_tag_member_decl_is_unsigned(info);
  token_kind_t mem_tag_kind = TK_EOF;
  char *mem_tag_name = NULL;
  int mem_tag_len = 0;
  int mem_tag_is_pointer = 0;
  psx_tag_member_decl_tag_identity(info, &mem_tag_kind, &mem_tag_name,
                                   &mem_tag_len, &mem_tag_is_pointer);
  deref->type_size = mem_storage_size ? mem_storage_size : (mem_size ? mem_size : 8);
  deref->deref_size = mem_deref_size;
  if (mem_array_len > 0 && mem_size > 0) {
    deref->type_size = mem_storage_size > 0 ? mem_storage_size : mem_size * mem_array_len;
    deref->deref_size = mem_size;
    deref->is_pointer = 1;
    deref->is_array_member = 1;
    if (mem_outer_stride > 0) {
      deref->deref_size = mem_outer_stride;
      deref->inner_deref_size = (short)mem_size;
      if (mem_mid_stride > 0) {
        deref->inner_deref_size = (short)mem_mid_stride;
        deref->next_deref_size = (short)mem_size;
      }
    }
    if (mem_is_ptr) {
      deref->is_tag_pointer = 0;
      deref->pointer_qual_levels = 1;
      deref->base_deref_size = (short)mem_deref_size;
      if (mem_ptr_array_pointee_bytes > 0) {
        deref->ptr_array_pointee_bytes = mem_ptr_array_pointee_bytes;
        int ptr_arr_elem =
            psx_tag_member_decl_ptr_array_pointee_elem_size(info);
        if (ptr_arr_elem > 0) deref->base_deref_size = (short)ptr_arr_elem;
        if (!psx_ctx_is_tag_aggregate_kind(mem_tag_kind)) {
          deref->pointee_is_scalar_ptr = 1;
        }
      }
    }
  } else if (mem_is_ptr && mem_size > 0 && mem_outer_stride > 0) {
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
    deref->deref_size = (short)mem_outer_stride;
    if (mem_mid_stride > 0) {
      deref->inner_deref_size = (short)mem_mid_stride;
      deref->next_deref_size = (short)mem_deref_size;
    } else {
      deref->inner_deref_size = (short)mem_deref_size;
    }
  } else if (mem_is_ptr && mem_size > 0) {
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
    if (psx_ctx_is_tag_aggregate_kind(mem_tag_kind) && mem_tag_name) {
      int pointee_size = psx_ctx_get_tag_size(mem_tag_kind, mem_tag_name,
                                              mem_tag_len);
      deref->pointer_qual_levels =
          mem_pointer_qual_levels > 0 ? mem_pointer_qual_levels : 1;
      deref->base_deref_size = (short)(pointee_size > 0 ? pointee_size : 8);
    }
    if (mem_ptr_array_pointee_bytes > 0) {
      deref->ptr_array_pointee_bytes = mem_ptr_array_pointee_bytes;
      int ptr_arr_elem =
          psx_tag_member_decl_ptr_array_pointee_elem_size(info);
      deref->base_deref_size =
          (short)(ptr_arr_elem > 0 ? ptr_arr_elem : mem_deref_size);
      deref->deref_size = 8;
      if (!psx_ctx_is_tag_aggregate_kind(mem_tag_kind)) {
        deref->pointee_is_scalar_ptr = 1;
      }
    }
  }
  deref->tag_kind = mem_tag_kind;
  deref->tag_name = mem_tag_name;
  deref->tag_len = mem_tag_len;
  deref->is_tag_pointer = mem_tag_is_pointer;
  if (psx_node_pointee_is_const_qualified(base) ||
      (!ps_node_is_pointer(base) && node_self_is_const_qualified(base))) {
    deref->is_const_qualified = 1;
  }
  if (psx_node_pointee_is_volatile_qualified(base) ||
      (!ps_node_is_pointer(base) && node_self_is_volatile_qualified(base))) {
    deref->is_volatile_qualified = 1;
  }
  deref->bit_width = info->bit_width;
  deref->bit_offset = info->bit_offset;
  deref->bit_is_signed = info->bit_is_signed;
  psx_decl_funcptr_sig_t member_funcptr_sig = psx_ctx_tag_member_funcptr_sig(info);
  if (psx_decl_funcptr_sig_has_payload(member_funcptr_sig) &&
      member_funcptr_sig.function.callable.return_shape.fp_kind == TK_FLOAT_KIND_NONE &&
      !member_funcptr_sig.function.callable.return_shape.is_data_pointer &&
      mem_fp_kind != TK_FLOAT_KIND_NONE) {
    member_funcptr_sig.function.callable.return_shape.fp_kind = mem_fp_kind;
  }
  node_mem_store_funcptr_signature(deref, &member_funcptr_sig);
  if (mem_fp_kind != TK_FLOAT_KIND_NONE) {
    if (mem_array_len > 0 && mem_size > 0)      deref->pointee_fp_kind = mem_fp_kind;
    else if (mem_is_ptr && mem_size > 0)        deref->pointee_fp_kind = mem_fp_kind;
    else                                       deref->base.fp_kind = mem_fp_kind;
  }
  if (mem_is_bool) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_bool = 1;
    else                                  deref->is_bool = 1;
  }
  if (mem_is_unsigned) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_unsigned = 1;
    else                                  deref->is_unsigned = 1;
  }
  if (info->decl_type) {
    psx_type_t *member_type =
        type_with_funcptr_sig_merged(info->decl_type, member_funcptr_sig);
    deref->base.type = type_with_self_qualifiers(member_type,
                                                deref->is_const_qualified,
                                                deref->is_volatile_qualified);
    sync_tag_member_mem_from_decl_type(deref, info, deref->base.type);
  } else {
    deref->base.type = type_from_mem(deref, mem_array_len > 0, 0);
  }
  return (node_t *)deref;
}

node_t *psx_node_new_unary_deref_for(node_t *operand) {
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_DEREF;
  node->base.lhs = operand;
  node->base.type = type_from_deref_operand(operand);
  node->base.fp_kind = TK_FLOAT_KIND_NONE;
  int ds = ps_node_deref_size(operand);
  node->type_size = ds ? ds : 8;
  int row_deref_normalized = 0;
  {
    int inner = 0;
    int next = 0;
    int extras[5] = {0};
    int extras_count = 0;
    if (ds > 0 && psx_node_pointer_qual_levels(operand) <= 1 &&
        psx_node_pointer_stride_metadata(operand, &inner, &next, extras, &extras_count) &&
        inner > 0 && ds > inner) {
	      node->deref_size = (short)inner;
	      node->inner_deref_size = (short)next;
	      if (extras_count > 0) {
        node->next_deref_size = (short)extras[0];
        for (int i = 1; i < extras_count && (i - 1) < 5; i++)
          node->extra_strides[i - 1] = extras[i];
        node->extra_strides_count = (unsigned char)(extras_count - 1);
      }
      if (!node->base.type || node->base.type->kind != PSX_TYPE_ARRAY)
        node->base.type = type_from_mem(node, 1, 0);
      if (extras_count <= 0 && next > 0 && node->next_deref_size <= 0 &&
          node->base.type) {
        int tail_stride = type_array_leaf_element_size(node->base.type);
        if (tail_stride <= 0) tail_stride = psx_type_deref_size(node->base.type);
        if (tail_stride > 0 && tail_stride < next)
          node->next_deref_size = (short)tail_stride;
      }
      row_deref_normalized = 1;
    }
  }

  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(operand, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  if (tag_kind != TK_EOF && is_tag_ptr) {
    node->tag_kind = tag_kind;
    node->tag_name = tag_name;
    node->tag_len = tag_len;
    if (row_deref_normalized) {
      node->is_tag_pointer = 0;
      if (!node->base.type || node->base.type->kind != PSX_TYPE_ARRAY)
        node->base.type = type_from_mem(node, 1, 0);
    } else {
      node->is_tag_pointer = (psx_node_pointer_qual_levels(operand) >= 2) ? 1 : 0;
      node->deref_size = 0;
    }
  }

  int pql = psx_node_pointer_qual_levels(operand);
  tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(operand);
  if (pql <= 1 && pointee_fp != TK_FLOAT_KIND_NONE) {
    node->base.fp_kind = pointee_fp;
  }
  if (pql <= 1 && psx_node_pointee_is_unsigned(operand)) {
    node->is_unsigned = 1;
  }

	  int operand_ptr_array_pointee_bytes = psx_node_ptr_array_pointee_bytes(operand);
	  if (operand_ptr_array_pointee_bytes > 0) {
	    int operand_base_deref_size = psx_node_base_deref_size(operand);
	    if (node->base_deref_size == 0 && operand_base_deref_size > 0) {
	      node->base_deref_size = (short)operand_base_deref_size;
	    }
	    psx_type_t *operand_type = psx_node_get_type(operand);
	    int result_carries_ptr_array_pointee =
	        type_carries_ptr_array_pointee_after_deref(node->base.type);
	    if (result_carries_ptr_array_pointee) {
	      node->ptr_array_pointee_bytes = operand_ptr_array_pointee_bytes;
	    }
	    int array_elem_size = operand_type && operand_type->base
	                              ? psx_type_sizeof(operand_type->base)
	                              : operand_base_deref_size;
    if (array_elem_size <= 0) array_elem_size = operand_base_deref_size;
    int operand_array_elem_is_scalar_pointer =
        operand_type && operand_type->base &&
        operand_type->base->kind == PSX_TYPE_POINTER &&
        !psx_type_is_tag_aggregate(operand_type->base->base);
    if (operand_array_elem_is_scalar_pointer &&
        operand_base_deref_size > 0 && operand_base_deref_size < array_elem_size) {
      node->base_deref_size = (short)operand_base_deref_size;
      node->pointee_is_scalar_ptr = 1;
    }
		    if (result_carries_ptr_array_pointee &&
		        !row_deref_normalized && operand_base_deref_size > 0 &&
		        operand_ptr_array_pointee_bytes > operand_base_deref_size &&
		        psx_node_pointer_qual_levels(operand) <= 1) {
	      node->type_size = operand_ptr_array_pointee_bytes;
	      node->deref_size = (short)array_elem_size;
	      node->inner_deref_size = 0;
	      node->is_tag_pointer = 0;
		      if (!node->base.type || node->base.type->kind != PSX_TYPE_ARRAY)
		        node->base.type = type_from_mem(node, 1, 0);
      row_deref_normalized = 1;
    }
  }
  if (psx_node_pointee_is_const_qualified(operand)) node->is_const_qualified = 1;
  if (psx_node_pointee_is_volatile_qualified(operand)) node->is_volatile_qualified = 1;

  if (pql >= 2) {
    node->is_pointer = 1;
    int new_pql = pql - 1;
    node->pointer_qual_levels = new_pql;
    int bds = psx_node_base_deref_size(operand);
    node->base_deref_size = (short)bds;
    node->deref_size = (new_pql >= 2) ? 8 : (short)bds;
    node->pointee_fp_kind = pointee_fp;
  }
  if (!sync_unary_deref_mem_from_scalar_type(node, node->base.type)) {
    sync_unary_deref_mem_from_pointer_type(node);
  }

  {
    node_t *probe = operand;
    while (probe && probe->kind == ND_ADD) probe = probe->lhs;
    if (probe && probe->kind == ND_LVAR) {
      lvar_t *src = psx_node_lvar_symbol(probe);
      if (src && !psx_node_get_type(probe) &&
          src->outer_stride > 0 && src->mid_stride == 0 && !src->is_array) {
        node->deref_size = (short)src->elem_size;
        if (src->tag_kind != TK_EOF && !src->is_tag_pointer && node->tag_kind == TK_EOF) {
          node->tag_kind = src->tag_kind;
          node->tag_name = src->tag_name;
          node->tag_len = src->tag_len;
          node->is_tag_pointer = 0;
        }
        if (src->pointer_qual_levels >= 1 && src->base_deref_size > 0) {
          node->pointer_qual_levels = src->pointer_qual_levels;
          node->base_deref_size = src->base_deref_size;
        }
        if (src->ptr_array_pointee_bytes > 0 &&
            type_carries_ptr_array_pointee_after_deref(node->base.type)) {
          node->ptr_array_pointee_bytes = src->ptr_array_pointee_bytes;
          if (node->base_deref_size == 0) node->base_deref_size = (short)src->elem_size;
        }
      }
    } else if (probe && probe->kind == ND_FUNCALL) {
      int inner = 0;
      int next = 0;
      psx_type_t *func_type = psx_node_get_type(probe);
      psx_ret_pointee_array_t func_ret_array =
          func_type ? func_type->funcptr_sig.function.callable.return_shape.pointee_array : (psx_ret_pointee_array_t){0};
      if (func_type && psx_ret_pointee_array_has_dims(func_ret_array) &&
          psx_node_pointer_stride_metadata(probe, &inner, &next, NULL, NULL)) {
        if (inner > 0) {
          node->deref_size = (short)inner;
          if (next > 0) node->inner_deref_size = (short)next;
        }
        if (psx_type_is_tag_aggregate(func_type->base)) {
          node->tag_kind = func_type->base->tag_kind;
          node->tag_name = func_type->base->tag_name;
          node->tag_len = func_type->base->tag_len;
          node->tag_scope_depth_p1 = func_type->base->tag_scope_depth_p1;
          node->is_tag_pointer = 0;
        }
      }
    } else if (probe && probe->kind == ND_DEREF) {
      int inner = 0;
      int next = 0;
      int probe_is_tag_ptr = 0;
      psx_node_get_tag_type(probe, NULL, NULL, NULL, &probe_is_tag_ptr);
      int probe_deref_size = ps_node_deref_size(probe);
      if (probe_is_tag_ptr &&
          psx_node_pointer_stride_metadata(probe, &inner, &next, NULL, NULL) &&
          inner > 0 && probe_deref_size > inner) {
        node->deref_size = (short)inner;
        if (next > 0) {
          node->inner_deref_size = (short)next;
        }
      }
    }
  }

  if (operand && operand->kind == ND_LVAR) {
    lvar_t *src = psx_node_lvar_symbol(operand);
    if (src && !psx_node_get_type(operand) &&
        src->outer_stride > 0 && src->mid_stride > 0) {
      node->deref_size = (short)src->mid_stride;
      if (src->tag_kind != TK_EOF && !src->is_tag_pointer && node->tag_kind == TK_EOF) {
        node->tag_kind = src->tag_kind;
        node->tag_name = src->tag_name;
        node->tag_len = src->tag_len;
        node->is_tag_pointer = 0;
      }
      if (src->extra_strides_count > 0) {
        node->inner_deref_size = (short)src->extra_strides[0];
        if (src->extra_strides_count > 1) {
          node->next_deref_size = (short)src->extra_strides[1];
          for (int i = 2; i < src->extra_strides_count && (i - 2) < 5; i++) {
            node->extra_strides[i - 2] = src->extra_strides[i];
          }
          if (src->extra_strides_count - 2 < 5) {
            node->extra_strides[src->extra_strides_count - 2] = src->elem_size;
            node->extra_strides_count = (unsigned char)(src->extra_strides_count - 1);
          } else {
            node->extra_strides_count = (unsigned char)(src->extra_strides_count - 2);
          }
        } else {
          node->next_deref_size = (short)src->elem_size;
        }
      } else {
        node->inner_deref_size = (short)src->elem_size;
      }
    }
  }

  if (node->deref_size == 0) {
    node_t *probe = operand;
    while (probe && (probe->kind == ND_ADD || probe->kind == ND_SUB)) probe = probe->lhs;
    int inner = 0;
    int next = 0;
    int extras[5] = {0};
    int extras_count = 0;
    if (psx_node_pointer_stride_metadata(probe, &inner, &next, extras, &extras_count) &&
        inner > 0) {
      node->deref_size = (short)inner;
      node->inner_deref_size = (short)next;
      if (extras_count > 0) {
        node->next_deref_size = (short)extras[0];
        for (int i = 1; i < extras_count && (i - 1) < 5; i++)
          node->extra_strides[i - 1] = extras[i];
        node->extra_strides_count = (unsigned char)(extras_count - 1);
      } else if (next > 0 && node->base.type) {
        int tail_stride = type_array_leaf_element_size(node->base.type);
        if (tail_stride <= 0) tail_stride = psx_type_deref_size(node->base.type);
        if (tail_stride > 0 && tail_stride < next)
          node->next_deref_size = (short)tail_stride;
      }
    }
  }

  if (node->deref_size > 0 && node->base.fp_kind != TK_FLOAT_KIND_NONE &&
      node->pointee_fp_kind == TK_FLOAT_KIND_NONE) {
    node->pointee_fp_kind = node->base.fp_kind;
  }
  if (row_deref_normalized) {
    if (!node->base.type || node->base.type->kind != PSX_TYPE_ARRAY)
      node->base.type = type_from_mem(node, 1, 0);
  }
  return (node_t *)node;
}

node_t *psx_node_new_subscript_deref_for(node_t *base, node_t *base_addr,
                                         node_t *scaled_offset,
                                         int elem_size, int inner_deref_size,
                                         int next_deref_size,
                                         const int *extra_strides,
                                         int extra_strides_count) {
  node_t *addr = psx_node_new_binary(ND_ADD, base_addr, scaled_offset);
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  deref->type_size = elem_size;
  deref->deref_size = inner_deref_size;
  deref->inner_deref_size = (short)next_deref_size;
  if (extra_strides_count > 0 && extra_strides) {
    deref->next_deref_size = (short)extra_strides[0];
    for (int i = 1; i < extra_strides_count && (i - 1) < 5; i++) {
      deref->extra_strides[i - 1] = extra_strides[i];
    }
    deref->extra_strides_count = (unsigned char)(extra_strides_count - 1);
  }
  deref->base.fp_kind = TK_FLOAT_KIND_NONE;

  int parent_vla_row = 0;
  int parent_remaining = 0;
  int parent_elem = 0;
  parent_vla_row = psx_node_vla_row_stride_frame_off(base);
  parent_remaining = node_vla_strides_remaining(base);
  psx_node_pointer_stride_metadata(base, &parent_elem, NULL, NULL, NULL);
  int vla_subscript_keeps_row = parent_vla_row != 0 && inner_deref_size > 0;
  if (parent_vla_row != 0) {
    if (parent_remaining > 0) {
      deref->vla_row_stride_frame_off = parent_vla_row + 8;
      deref->vla_strides_remaining = parent_remaining - 1;
    }
    if (parent_elem > 0) {
      deref->inner_deref_size = (short)parent_elem;
      deref->next_deref_size = (short)parent_elem;
    }
    if (vla_subscript_keeps_row && deref->type_size <= deref->deref_size) {
      deref->type_size = (short)(inner_deref_size * 2);
    }
  }

  int pql = psx_node_pointer_qual_levels(base);
  int bds = psx_node_base_deref_size(base);
  int base_addr_bds = psx_node_base_deref_size(base_addr);
  int base_ptr_array_pointee_bytes = psx_node_ptr_array_pointee_bytes(base);
  psx_ret_pointee_array_t base_ret_array = {0};
  psx_type_t *base_type = psx_node_get_type(base);
  if (base_type) base_ret_array = base_type->funcptr_sig.function.callable.return_shape.pointee_array;
  int deref_type_is_canonical = 0;
  int base_array_element_is_pointer =
      base_type && base_type->kind == PSX_TYPE_ARRAY &&
      type_is_pointer_view_type(base_type->base);
  int base_array_element_is_tag =
      base_type && base_type->kind == PSX_TYPE_ARRAY &&
      psx_type_is_tag_aggregate(base_type->base) &&
      !(inner_deref_size > 0 && elem_size > inner_deref_size);
  int base_is_pointer_to_array_view =
      base_type && base_type->kind == PSX_TYPE_POINTER &&
      base_type->ptr_array_pointee_bytes <= 0 &&
      base_type->outer_stride > 0 && inner_deref_size > 0 &&
      elem_size > inner_deref_size;
  int base_is_ret_pointee_array =
      psx_ret_pointee_array_has_dims(base_ret_array) ? 1 : 0;
  int base_is_unary_ptr_array_deref =
      base && base->kind == ND_DEREF && base->lhs &&
      psx_node_ptr_array_pointee_bytes(base->lhs) > 0;
  psx_type_t *canonical_subscript_type =
      type_from_subscript_base_type(base_type, elem_size, inner_deref_size,
                                    next_deref_size, extra_strides,
                                    extra_strides_count);
  if (!canonical_subscript_type && base_type &&
      base_type->kind == PSX_TYPE_POINTER && base_type->base &&
      base_type->base->kind != PSX_TYPE_ARRAY &&
      !node_scalar_ptr_member_lvalue(base)) {
    int base_size = psx_type_sizeof(base_type->base);
    if (base_size <= 0 || elem_size <= 0 || elem_size == base_size) {
      canonical_subscript_type =
          type_with_funcptr_sig(base_type->base, base_type->funcptr_sig);
    }
  }

  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(base, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  if (tag_kind == TK_EOF && base_is_unary_ptr_array_deref) {
    psx_node_get_tag_type(base_addr, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  }
  if (tag_kind != TK_EOF) {
    deref->tag_kind = tag_kind;
    deref->tag_name = tag_name;
    deref->tag_len = tag_len;
    deref->is_tag_pointer = 0;
  }

  int subscript_is_intermediate_row =
      (inner_deref_size > 0 && elem_size > inner_deref_size &&
       !base_array_element_is_pointer &&
       !base_array_element_is_tag) ||
      vla_subscript_keeps_row;
  if (base_array_element_is_tag && !subscript_is_intermediate_row &&
      base_type && base_type->base) {
    canonical_subscript_type = base_type->base;
  }
  int deref_from_pointer_to_array = 0;
  if (base->kind == ND_DEREF && base->lhs) {
    node_t *probe = base->lhs;
    while (probe && probe->kind == ND_ADD) probe = probe->lhs;
    if (probe && probe->kind == ND_LVAR) {
      psx_type_t *probe_type = psx_node_get_type(probe);
      if (type_is_pointer_to_array_type(probe_type) ||
          (probe_type && probe_type->kind == PSX_TYPE_POINTER &&
           probe_type->outer_stride > 0)) {
        deref_from_pointer_to_array = 1;
      } else if (!probe_type) {
        lvar_t *src = psx_node_lvar_symbol(probe);
        if (src && src->outer_stride > 0 && !src->is_array)
          deref_from_pointer_to_array = 1;
      }
    }
  }
  if (pql >= 1 && bds > 0 && subscript_is_intermediate_row) {
    deref->pointer_qual_levels =
        (vla_subscript_keeps_row || base_is_pointer_to_array_view) ? 0 : pql;
    if (base_is_pointer_to_array_view) deref->is_pointer = 0;
    deref->base_deref_size = (short)bds;
  } else if (pql == 1 && bds > 0 && base_is_ret_pointee_array) {
    deref->pointer_qual_levels = 0;
    deref->base_deref_size = 0;
    deref->deref_size = 0;
    deref->is_pointer = 0;
    deref->is_tag_pointer = 0;
  } else if (pql == 1 && bds > 0 &&
             (base->kind == ND_LVAR || base->kind == ND_GVAR ||
              base->kind == ND_FUNCALL || base->kind == ND_CAST ||
              (base->kind == ND_DEREF &&
               !deref_from_pointer_to_array &&
               base_ptr_array_pointee_bytes == 0 &&
               (!(base->lhs && base->lhs->kind == ND_ADD) ||
                node_scalar_ptr_member_lvalue(base))))) {
    deref->deref_size = 0;
  } else if (pql >= 1 && bds > 0) {
    deref->is_pointer = 1;
    int result_pql = pql;
    if ((base->kind == ND_LVAR || base->kind == ND_GVAR ||
         base->kind == ND_FUNCALL || base->kind == ND_CAST ||
         (base->kind == ND_DEREF && node_legacy_scalar_ptr_member(base))) &&
        pql >= 2) {
      result_pql = pql - 1;
    }
    deref->pointer_qual_levels = result_pql;
    deref->base_deref_size = (result_pql >= 2) ? (short)bds : 0;
    deref->deref_size = (result_pql >= 2) ? 8 : (short)bds;
    if (deref->tag_kind != TK_EOF) {
      deref->is_tag_pointer = 1;
    }
    deref->pointee_fp_kind = psx_node_pointee_fp_kind(base);
  }
  if (pql == 1 && parent_vla_row != 0 && inner_deref_size > 0 &&
      deref->deref_size == 0) {
    deref->deref_size = (short)inner_deref_size;
  }

  {
    tk_float_kind_t arr_pointee_fp = psx_node_pointee_fp_kind(base);
    int node_vla_rsf = psx_node_vla_row_stride_frame_off(base);
    int is_vla_row = (node_vla_rsf != 0 && inner_deref_size > 0);
    if (arr_pointee_fp != TK_FLOAT_KIND_NONE && pql == 0) {
      if ((inner_deref_size > 0 && elem_size > inner_deref_size) || is_vla_row) {
        deref->pointee_fp_kind = arr_pointee_fp;
      } else if (inner_deref_size == 0 && bds > 0) {
        deref->pointee_fp_kind = arr_pointee_fp;
        deref->is_pointer = 1;
        deref->deref_size = 8;
      } else {
        deref->base.fp_kind = arr_pointee_fp;
      }
    }
  }
  if (pql == 1) {
    tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(base);
    if (pointee_fp != TK_FLOAT_KIND_NONE) {
      int remains_array_row = inner_deref_size > 0 &&
                              ((elem_size > inner_deref_size) ||
                               parent_vla_row != 0);
      if (remains_array_row) deref->pointee_fp_kind = pointee_fp;
      else                   deref->base.fp_kind = pointee_fp;
    }
  }
  int scalar_elem_size = bds > 0 ? bds : base_addr_bds;
  if (pql == 0 && inner_deref_size == 0 &&
      scalar_elem_size > 0 && elem_size >= scalar_elem_size) {
    deref->type_size = scalar_elem_size;
    deref->deref_size = 0;
    tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(base);
    if (pointee_fp == TK_FLOAT_KIND_NONE)
      pointee_fp = psx_node_pointee_fp_kind(base_addr);
    if (pointee_fp != TK_FLOAT_KIND_NONE) deref->base.fp_kind = pointee_fp;
  }

  if (base_array_element_is_pointer && !subscript_is_intermediate_row && base_type->base) {
    deref->base.type = type_with_funcptr_sig(base_type->base,
                                             base_type->funcptr_sig);
    sync_unary_deref_mem_from_pointer_type(deref);
    if (base_type->base->kind == PSX_TYPE_POINTER &&
        base_type->base->base &&
        base_type->base->base->kind == PSX_TYPE_ARRAY) {
      int array_size = psx_type_sizeof(base_type->base->base);
      int elem_stride = psx_type_deref_size(base_type->base->base);
      if (array_size > 0) deref->deref_size = (short)array_size;
      if (elem_stride > 0) {
        deref->inner_deref_size = (short)elem_stride;
        deref->base_deref_size = (short)elem_stride;
      }
    }
    deref->ptr_array_pointee_bytes = 0;
    deref->next_deref_size = 0;
    deref->extra_strides_count = 0;
    for (int i = 0; i < 5; i++) deref->extra_strides[i] = 0;
    deref_type_is_canonical = 1;
  }

  int vla_intermediate_row =
      vla_subscript_keeps_row &&
      (!canonical_subscript_type ||
       canonical_subscript_type->kind != PSX_TYPE_ARRAY);
  if (vla_intermediate_row) {
    deref->base.type = type_from_mem(deref, 1, 1);
    deref_type_is_canonical = deref->base.type != NULL;
  }

  if (!deref_type_is_canonical && canonical_subscript_type) {
    deref->base.type = canonical_subscript_type;
    if (canonical_subscript_type->kind == PSX_TYPE_POINTER) {
      sync_unary_deref_mem_from_pointer_type(deref);
    } else if (sync_unary_deref_mem_from_scalar_type(deref,
                                                     canonical_subscript_type)) {
      /* Scalar canonical type wins over earlier legacy pointer metadata. */
    } else if (canonical_subscript_type->kind == PSX_TYPE_ARRAY) {
      deref->is_pointer = 0;
      deref->is_tag_pointer = 0;
      deref->pointer_qual_levels = 0;
      if (parent_vla_row == 0) {
        deref->type_size = psx_type_sizeof(canonical_subscript_type);
        deref->deref_size = psx_type_deref_size(canonical_subscript_type);
        deref->inner_deref_size = (short)canonical_subscript_type->outer_stride;
        deref->next_deref_size = (short)canonical_subscript_type->mid_stride;
        deref->extra_strides_count = canonical_subscript_type->extra_strides_count;
        for (int i = 0; i < 5; i++)
          deref->extra_strides[i] = canonical_subscript_type->extra_strides[i];
      }
    }
    deref_type_is_canonical = 1;
  }

	  {
	    if (!deref_type_is_canonical && base_ptr_array_pointee_bytes > 0 && bds > 0) {
	      if (base_is_unary_ptr_array_deref && base_array_element_is_pointer &&
	          elem_size < base_ptr_array_pointee_bytes) {
	        deref->is_pointer = 1;
	        deref->is_tag_pointer = 0;
	        deref->type_size = 8;
	        deref->deref_size = (short)bds;
	        deref->inner_deref_size = 0;
	        deref->pointer_qual_levels = 1;
	        deref->base_deref_size = (short)bds;
	        deref->ptr_array_pointee_bytes = 0;
	      } else if (base_is_unary_ptr_array_deref &&
	                 !base_array_element_is_pointer &&
	                 tag_kind != TK_EOF &&
	                 elem_size <= bds) {
	        deref->is_pointer = 0;
	        deref->is_tag_pointer = 0;
	        deref->deref_size = 0;
	        deref->inner_deref_size = 0;
	        deref->pointer_qual_levels = 0;
	        deref->base_deref_size = 0;
	        deref->ptr_array_pointee_bytes = 0;
	      } else if (subscript_is_intermediate_row) {
	        if (elem_size > base_ptr_array_pointee_bytes) {
	          deref->ptr_array_pointee_bytes = base_ptr_array_pointee_bytes;
	        } else {
          deref->pointer_qual_levels = 0;
          deref->is_pointer = 0;
          deref->is_tag_pointer = 0;
        }
        deref->base_deref_size = (short)bds;
      } else if (!base_is_unary_ptr_array_deref || elem_size > bds) {
        deref->is_tag_pointer = 1;
        deref->is_pointer = 1;
        deref->type_size = 8;
        deref->deref_size = (short)base_ptr_array_pointee_bytes;
        deref->inner_deref_size = (short)bds;
        deref->pointer_qual_levels = 0;
        deref->base_deref_size = 0;
      }
    }
  }
  if (base_ptr_array_pointee_bytes > 0 && base_is_unary_ptr_array_deref &&
      base_array_element_is_tag && !subscript_is_intermediate_row) {
    deref->is_pointer = 0;
    deref->is_tag_pointer = 0;
    deref->deref_size = 0;
    deref->inner_deref_size = 0;
    deref->pointer_qual_levels = 0;
    deref->base_deref_size = 0;
    deref->ptr_array_pointee_bytes = 0;
  }

  {
    node_mem_t *base_mem = base && (base->kind == ND_ADDR || base->kind == ND_LVAR ||
                                    base->kind == ND_GVAR || base->kind == ND_DEREF)
                              ? (node_mem_t *)base : NULL;
    if (psx_node_pointee_is_bool(base) || psx_node_pointee_is_bool(base_addr)) {
      if (pql == 0 && inner_deref_size == 0) {
        deref->is_bool = 1;
      } else {
        deref->pointee_is_bool = 1;
      }
    }
    if (psx_node_pointee_is_unsigned(base)) {
      int is_final_scalar = !deref->is_pointer &&
                            !(inner_deref_size > 0 && elem_size > inner_deref_size);
      if (is_final_scalar) deref->is_unsigned = 1;
      else                 deref->pointee_is_unsigned = 1;
    }
    if (subscript_is_intermediate_row || !base_is_ret_pointee_array) {
      psx_node_copy_funcptr_metadata(deref, base);
    }
    if (psx_node_mem_has_funcptr_metadata(deref) && inner_deref_size == 0) {
      deref->is_pointer = 1;
      deref->type_size = 8;
      if (deref->deref_size == 0) deref->deref_size = 8;
    }
    if (psx_node_pointee_is_const_qualified(base) ||
        psx_node_pointee_is_const_qualified(base_addr) ||
        (node_is_array_view(base) && node_self_is_const_qualified(base))) {
      deref->is_const_qualified = 1;
    }
    if (psx_node_pointee_is_volatile_qualified(base) ||
        psx_node_pointee_is_volatile_qualified(base_addr) ||
        (node_is_array_view(base) && node_self_is_volatile_qualified(base))) {
      deref->is_volatile_qualified = 1;
    }
    if (base_mem && !base->type && base_mem->is_unsigned && !deref->is_unsigned &&
        !deref->is_pointer && pql == 0 && inner_deref_size == 0) {
      deref->is_unsigned = 1;
    }
    node_mem_t *base_pointee_scalar_ptr_mem =
        node_legacy_pointee_scalar_ptr_mem(base);
    if (base_pointee_scalar_ptr_mem && pql == 0) {
      if (inner_deref_size == 0) {
        deref->is_scalar_ptr_member = 1;
        deref->is_pointer = 1;
        int pelem = 0;
        if (base->kind == ND_ADDR && base->lhs && base->lhs->kind == ND_GVAR) {
          node_gvar_t *gv_node = (node_gvar_t *)base->lhs;
          for (global_var_t *gv = psx_find_global_var(gv_node->name, gv_node->name_len); gv; gv = NULL) {
            if (gv->name_len == gv_node->name_len &&
                memcmp(gv->name, gv_node->name, (size_t)gv->name_len) == 0) {
              pelem = gv->pointee_elem_size;
              break;
            }
          }
        }
        if (pelem == 0) pelem = base_pointee_scalar_ptr_mem->base_deref_size;
        if (pelem > 0) deref->deref_size = pelem;
      } else {
        deref->pointee_is_scalar_ptr = 1;
        deref->base_deref_size = base_pointee_scalar_ptr_mem->base_deref_size;
      }
    }
  }
  if (canonical_subscript_type && !vla_intermediate_row) {
    deref->base.type = canonical_subscript_type;
    if (canonical_subscript_type->kind == PSX_TYPE_POINTER) {
      sync_unary_deref_mem_from_pointer_type(deref);
    } else {
      (void)sync_unary_deref_mem_from_scalar_type(deref,
                                                  canonical_subscript_type);
    }
  } else if (!subscript_is_intermediate_row && !deref_type_is_canonical) {
    deref->base.type = type_from_mem(deref, 0, 0);
  }
  return (node_t *)deref;
}

node_t *psx_node_new_byref_param_deref_for(lvar_t *var) {
  node_lvar_t *ptr_lvar = (node_lvar_t *)psx_node_new_lvar_typed_for(var, 8);
  psx_type_t *value_type = psx_lvar_get_decl_type(var);
  if (value_type) {
    int deref_size = var && var->elem_size > 0 ? var->elem_size
                     : psx_type_sizeof(value_type);
    psx_type_t *ptr_type = psx_type_new_pointer(value_type, deref_size);
    ptr_lvar->mem.base.type = ptr_type;
    ptr_lvar->mem.is_pointer = 1;
    ptr_lvar->mem.deref_size = (short)deref_size;
    ptr_lvar->mem.base_deref_size = (short)deref_size;
    ptr_lvar->mem.is_tag_pointer = psx_type_is_tag_aggregate(value_type) ? 1 : 0;
  }
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = (node_t *)ptr_lvar;
  deref->base.type = value_type;
  deref->type_size = var->elem_size;
  deref->tag_kind = var->tag_kind;
  deref->tag_name = var->tag_name;
  deref->tag_len = var->tag_len;
  deref->is_tag_pointer = 0;
  return (node_t *)deref;
}

node_t *psx_node_new_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                         int member_type_size, token_kind_t member_tag_kind,
                                         char *member_tag_name, int member_tag_len,
                                         int member_is_tag_pointer) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(
      (owner ? owner->offset : 0) + member_offset, member_type_size);
  node->var = owner;
  node->mem.tag_kind = member_tag_kind;
  node->mem.tag_name = member_tag_name;
  node->mem.tag_len = member_tag_len;
  node->mem.is_tag_pointer = member_is_tag_pointer ? 1 : 0;
  return (node_t *)node;
}

static int tag_member_ref_deref_size_from_type(const tag_member_info_t *info,
                                               const psx_type_t *type) {
  (void)info;
  if (!type || type->kind != PSX_TYPE_POINTER) return 0;
  if (type->ptr_array_pointee_bytes > 0) return type->ptr_array_pointee_bytes;
  if (type->outer_stride > 0) return type->outer_stride;
  return psx_type_deref_size(type);
}

static void sync_tag_member_mem_from_decl_type(node_mem_t *mem,
                                               const tag_member_info_t *info,
                                               psx_type_t *type) {
  if (!mem || !type) {
    return;
  }
  if (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY) {
    int type_size = psx_type_sizeof(type);
    if (type_size > 0) mem->type_size = (short)type_size;
    sync_scalar_mem_from_decl_type(mem, type);
    return;
  }
  clear_pointer_payload_mem(mem);
  mem->is_pointer = 1;
  int type_size = psx_type_sizeof(type);
  if (type_size > 0) mem->type_size = (short)type_size;
  int deref_size = type->kind == PSX_TYPE_ARRAY
                       ? psx_type_deref_size(type)
                       : tag_member_ref_deref_size_from_type(info, type);
  if (deref_size > 0) mem->deref_size = (short)deref_size;
  if (type->kind == PSX_TYPE_ARRAY) mem->is_array_member = 1;

  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  int has_stride = node_pointer_stride_from_type(type, &inner_stride,
                                                 &next_stride, extra_strides,
                                                 &extra_count);
  if (!has_stride && type->kind == PSX_TYPE_ARRAY && deref_size > 0) {
    inner_stride = deref_size;
    next_stride = 0;
    extra_count = 0;
    has_stride = 1;
  }
  if (has_stride) {
    mem->inner_deref_size = (short)inner_stride;
    mem->next_deref_size = (short)next_stride;
    mem->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      mem->extra_strides[i] = extra_strides[i];
    for (int i = extra_count; i < 5; i++)
      mem->extra_strides[i] = 0;
  }

  int base_deref_size = type_pointer_view_base_deref_size(type, 1);
  if (base_deref_size > 0) mem->base_deref_size = (short)base_deref_size;
  if (type->pointer_qual_levels > 0)
    mem->pointer_qual_levels = (unsigned char)type->pointer_qual_levels;
  if (type->ptr_array_pointee_bytes > 0)
    mem->ptr_array_pointee_bytes = type->ptr_array_pointee_bytes;

  mem->pointee_fp_kind = (unsigned int)type_deep_pointee_fp_kind(type);
  sync_pointee_flags_mem_from_type(mem, type);
  sync_tag_mem_from_decl_type(mem, type);
}

node_t *psx_node_new_tag_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                             const tag_member_info_t *info) {
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_pointer = 0;
  if (info) {
    psx_tag_member_decl_tag_identity(info, &tag_kind, &tag_name, &tag_len,
                                     &is_tag_pointer);
  }
  node_lvar_t *node = (node_lvar_t *)psx_node_new_member_lvar_ref_for(
      owner, member_offset, info ? psx_tag_member_decl_value_size(info) : 0,
      tag_kind, tag_name, tag_len, is_tag_pointer);
  if (info && info->decl_type) {
    psx_type_t *member_type =
        type_with_funcptr_sig_merged(info->decl_type,
                                     psx_ctx_tag_member_funcptr_sig(info));
    node->mem.base.type = type_with_self_qualifiers(
        member_type,
        owner && owner->is_const_qualified,
        owner && owner->is_volatile_qualified);
    sync_tag_member_mem_from_decl_type(&node->mem, info, node->mem.base.type);
    if (owner && owner->is_const_qualified) node->mem.is_const_qualified = 1;
    if (owner && owner->is_volatile_qualified) node->mem.is_volatile_qualified = 1;
  }
  if (info) {
    psx_node_copy_funcptr_metadata_from_tag_member(&node->mem, info);
    if (!info->decl_type) {
      tk_float_kind_t fp_kind = psx_tag_member_decl_fp_kind(info);
      if (!is_tag_pointer && fp_kind != TK_FLOAT_KIND_NONE) {
        node->mem.base.fp_kind = fp_kind;
      }
      if (psx_tag_member_decl_is_bool(info)) {
        node->mem.is_bool = 1;
      }
      if (psx_tag_member_decl_is_unsigned(info)) {
        node->mem.base.is_unsigned = 1;
        node->mem.is_unsigned = 1;
      }
    }
  }
  if (info && info->bit_width > 0) {
    node->mem.bit_width = info->bit_width;
    node->mem.bit_offset = info->bit_offset;
    node->mem.bit_is_signed = info->bit_is_signed;
  }
  return (node_t *)node;
}

node_t *psx_node_new_gvar_for(global_var_t *gv) {
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  psx_node_init_gvar_ref_metadata(&node->mem, gv);
  if (gv) {
    node->name = gv->name;
    node->name_len = gv->name_len;
    node->is_thread_local = gv->is_thread_local ? 1 : 0;
  }
  return (node_t *)node;
}

node_t *psx_node_new_gvar_array_base_for(global_var_t *gv) {
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  psx_node_init_gvar_array_base_metadata(&node->mem, gv);
  if (gv) {
    node->name = gv->name;
    node->name_len = gv->name_len;
    node->is_thread_local = gv->is_thread_local ? 1 : 0;
  }
  return (node_t *)node;
}

node_t *psx_node_new_static_local_gvar_for(lvar_t *var, int type_size) {
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  psx_node_init_static_local_gvar_ref_metadata(&node->mem, var, type_size);
  if (var) {
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
  if (!target || !is_mem_node_kind(target->kind)) return target;
  node_mem_t *clone = arena_alloc(sizeof(node_mem_t));
  *clone = *(node_mem_t *)target;
  clone->base.lhs = lhs;
  return (node_t *)clone;
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
  if (lhs->type) return 0;
  node_mem_t *mem = node_mem_view(lhs);
  if (mem && mem->pointee_is_bool && mem->deref_size <= 1) return 1;
  return mem && mem->is_bool ? 1 : 0;
}

static int node_legacy_scalar_ptr_member(node_t *node) {
  node_mem_t *mem = node_mem_view(node);
  return node && !node->type && mem && mem->is_scalar_ptr_member;
}

static int node_scalar_ptr_member_lvalue(node_t *node) {
  node_mem_t *mem = node_mem_view(node);
  return node && node->kind == ND_DEREF && mem && mem->is_scalar_ptr_member;
}

static node_mem_t *node_legacy_pointee_scalar_ptr_mem(node_t *node) {
  node_mem_t *mem = node_mem_view(node);
  if (!node || node->type || !mem || !mem->pointee_is_scalar_ptr) return NULL;
  return mem;
}

int psx_node_scalar_ptr_member_lvalue(node_t *node) {
  return node_scalar_ptr_member_lvalue(node);
}

int psx_node_legacy_pointee_scalar_ptr(node_t *node) {
  return node_legacy_pointee_scalar_ptr_mem(node) != NULL;
}

int psx_node_subscript_deref_uses_base_address(node_t *node) {
  if (!node || node->kind != ND_DEREF) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->kind == PSX_TYPE_ARRAY) return 1;
  node_mem_t *mem = node_mem_view(node);
  if (!mem) return 0;
  if (mem->deref_size > 0 && !mem->is_pointer) return 1;
  if (mem->vla_row_stride_frame_off > 0 && !mem->is_pointer) return 1;
  return 0;
}

int psx_node_deref_decays_to_address(node_t *node) {
  if (!node || node->kind != ND_DEREF) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type) return type->kind == PSX_TYPE_ARRAY;

  node_mem_t *mem = node_mem_view(node);
  if (!mem || mem->deref_size <= 0) return 0;
  if (mem->type_size > 8 || mem->is_array_member) return 1;
  if (mem->is_pointer && mem->pointer_qual_levels == 0 &&
      !mem->is_scalar_ptr_member && mem->type_size > mem->deref_size &&
      mem->pointee_fp_kind == TK_FLOAT_KIND_NONE && mem->inner_deref_size == 0) {
    return 1;
  }
  if (!mem->is_pointer && mem->tag_kind == TK_EOF &&
      mem->type_size > mem->deref_size) {
    return 1;
  }
  return 0;
}

psx_type_t *psx_node_row_decay_pointer_arith_type(node_t *node) {
  if (!node || (node->kind != ND_DEREF && node->kind != ND_ADDR)) return NULL;
  int ds = ps_node_deref_size(node);
  if (ds <= 0 || ps_node_type_size(node) <= ds) return NULL;

  psx_type_t *type = psx_node_get_type(node);
  psx_type_t *base = (type && type->kind == PSX_TYPE_ARRAY && type->base)
                         ? type->base
                         : NULL;
  if (!base) {
    if (node->type) return NULL;
    node_mem_t *mem = node_mem_view(node);
    if (!mem) return NULL;
    if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind, ds);
    } else {
      base = psx_type_new_integer(mem->pointee_is_bool ? TK_BOOL : TK_EOF,
                                  ds, mem->pointee_is_unsigned);
    }
  }

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
  node_mem_t *mem = node_mem_view(node);
  return (mem && mem->compound_literal_array_size > 0)
             ? mem->compound_literal_array_size
             : 0;
}

int psx_node_bitfield_width(node_t *node) {
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->bit_width : 0;
}

int psx_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed) {
  node_mem_t *mem = node_mem_view(node);
  if (!mem || mem->bit_width <= 0) return 0;
  if (bit_width) *bit_width = mem->bit_width;
  if (bit_offset) *bit_offset = mem->bit_offset;
  if (bit_is_signed) *bit_is_signed = mem->bit_is_signed;
  return 1;
}

int psx_node_value_is_pointer_like(node_t *node) {
  if (!node) return 0;
  if (node->type) return ps_node_is_pointer(node);
  if (ps_node_is_pointer(node)) return 1;
  node_mem_t *mem = node_mem_view(node);
  if (mem && mem->base.type && !psx_type_is_pointer(mem->base.type)) return 0;
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
  node_mem_t *mem = node_mem_view(node);
  if (!mem) return 0;
  if (descriptor_frame_off) *descriptor_frame_off = mem->type_size;
  if (row_stride_frame_off) *row_stride_frame_off = mem->vla_row_stride_frame_off;
  return mem->type_size > 0;
}

node_t *psx_node_new_vla_alloc(int descriptor_frame_off,
                               int row_stride_frame_off,
                               node_t *lhs, node_t *rhs) {
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_VLA_ALLOC;
  node->base.lhs = lhs;
  node->base.rhs = rhs;
  node->type_size = descriptor_frame_off;
  node->vla_row_stride_frame_off = row_stride_frame_off;
  return (node_t *)node;
}

node_mem_t *psx_node_new_assign(node_t *lhs, node_t *rhs) {
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
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_ASSIGN;
  node->base.lhs = lhs;
  node->base.rhs = (lhs_is_bool_slot(lhs) && rhs)
                       ? psx_node_new_binary(ND_NE, rhs, psx_node_new_num(0))
                       : rhs;
  node->base.type = psx_node_get_type(lhs);
  node->type_size = ps_node_type_size(lhs);
  sync_scalar_mem_from_decl_type(node, node->base.type);
  sync_pointer_cast_mem_from_type(node, node->base.type);
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
  node_mem_t *assign_node = psx_node_new_assign(lhs, op_expr);
  return (node_t *)assign_node;
}
