#include "node_utils.h"
#include "decl.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "arena.h"
#include "diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <string.h>

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
static inline token_t *curtok(void) { return tk_get_current_token(); }
static node_mem_t *node_mem_view(node_t *node);
static int type_is_pointer_view_type(const psx_type_t *type);
static psx_type_t *type_new_void(void);

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
static int node_value_view_from_node_direct(node_t *node, node_value_view_field_t field,
                                            int *value);
static int node_self_is_const_qualified(node_t *node);
static int node_self_is_volatile_qualified(node_t *node);
static int node_is_array_view(node_t *node);
static int node_legacy_scalar_ptr_member(node_t *node);
static int node_scalar_ptr_member_lvalue(node_t *node);
static node_mem_t *node_legacy_pointee_scalar_ptr_mem(node_t *node);

static int is_mem_node_kind(node_kind_t kind) {
  return kind == ND_LVAR || kind == ND_GVAR || kind == ND_DEREF ||
         kind == ND_ASSIGN || kind == ND_ADDR || kind == ND_STRING ||
         kind == ND_CAST;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_mem(const node_mem_t *mem) {
  if (!mem) return (psx_decl_funcptr_sig_t){0};
  return mem->funcptr_sig;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_type(const psx_type_t *type) {
  if (!type) return (psx_decl_funcptr_sig_t){0};
  return type->funcptr_sig;
}

static int funcptr_sig_has_return_shape(psx_decl_funcptr_sig_t sig) {
  return sig.ret_is_void || sig.ret_is_complex || sig.ret_is_data_pointer ||
         sig.ret_int_width > 0 || sig.ret_fp_kind != TK_FLOAT_KIND_NONE ||
         sig.ret_pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         psx_ret_pointee_array_has_dims(sig.ret_pointee_array);
}

static psx_decl_funcptr_sig_t funcptr_sig_from_lvar(const lvar_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  return src->funcptr_sig;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_gvar(const global_var_t *src) {
  if (!src) return (psx_decl_funcptr_sig_t){0};
  return src->funcptr_sig;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_tag_member(const tag_member_info_t *src) {
  return psx_ctx_tag_member_funcptr_sig(src);
}

static psx_decl_funcptr_sig_t funcptr_sig_merge_missing(psx_decl_funcptr_sig_t merged,
                                                        const psx_decl_funcptr_sig_t *sig,
                                                        int copy_variadic) {
  if (!sig) return merged;
  if (!merged.param_fp_mask && sig->param_fp_mask)
    merged.param_fp_mask = sig->param_fp_mask;
  if (!merged.param_int_mask && sig->param_int_mask)
    merged.param_int_mask = sig->param_int_mask;
  if (!merged.ret_int_width && sig->ret_int_width)
    merged.ret_int_width = sig->ret_int_width;
  if (merged.ret_fp_kind == TK_FLOAT_KIND_NONE && sig->ret_fp_kind != TK_FLOAT_KIND_NONE)
    merged.ret_fp_kind = sig->ret_fp_kind;
  if (merged.ret_pointee_fp_kind == TK_FLOAT_KIND_NONE &&
      sig->ret_pointee_fp_kind != TK_FLOAT_KIND_NONE)
    merged.ret_pointee_fp_kind = sig->ret_pointee_fp_kind;
  if (!merged.nargs_fixed && sig->nargs_fixed)
    merged.nargs_fixed = sig->nargs_fixed;
  if (copy_variadic && sig->is_variadic) merged.is_variadic = 1;
  if (sig->ret_is_void) merged.ret_is_void = 1;
  if (sig->ret_is_data_pointer) merged.ret_is_data_pointer = 1;
  if (sig->ret_is_complex) merged.ret_is_complex = 1;
  if (!psx_ret_pointee_array_has_dims(merged.ret_pointee_array) &&
      psx_ret_pointee_array_has_dims(sig->ret_pointee_array)) {
    merged.ret_pointee_array = sig->ret_pointee_array;
  }
  return merged;
}

static void node_mem_store_funcptr_signature(node_mem_t *dst,
                                             const psx_decl_funcptr_sig_t *sig) {
  if (!dst || !sig) return;
  dst->funcptr_sig = *sig;
}

void psx_node_store_funcptr_metadata(node_mem_t *dst, psx_decl_funcptr_sig_t sig) {
  node_mem_store_funcptr_signature(dst, &sig);
}

psx_decl_funcptr_sig_t psx_node_funcdef_ret_funcptr_sig(const node_func_t *fn) {
  if (!fn) return (psx_decl_funcptr_sig_t){0};
  return fn->ret_funcptr_sig;
}

void psx_node_funcdef_set_ret_funcptr_sig(node_func_t *fn, psx_decl_funcptr_sig_t sig) {
  if (!fn) return;
  fn->ret_funcptr_sig = sig;
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

static psx_type_t *type_new_pointee_base_from_mem(const node_mem_t *mem) {
  if (!mem) return NULL;
  if (mem->pointee_is_scalar_ptr) {
    int base_size = mem->base_deref_size > 0 ? mem->base_deref_size : 4;
    psx_type_t *base = psx_type_new_integer(mem->pointee_is_bool ? TK_BOOL : TK_EOF,
                                            base_size,
                                            mem->pointee_is_unsigned);
    psx_type_t *ptr = psx_type_new_pointer(base, base_size);
    ptr->base_deref_size = base_size;
    ptr->pointer_qual_levels = 1;
    return ptr;
  }
  if (mem->pointee_is_void) return type_new_void();
  if (mem->tag_kind == TK_STRUCT || mem->tag_kind == TK_UNION) {
    return psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                            mem->tag_scope_depth_p1, mem->deref_size);
  }
  if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    int sz = mem->deref_size > 0 ? mem->deref_size : 8;
    return psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind, sz);
  }
  int sz = mem->deref_size > 0 ? mem->deref_size : mem->base_deref_size;
  if (sz <= 0 || sz > 8) sz = 4;
  return psx_type_new_integer(mem->pointee_is_bool ? TK_BOOL : TK_EOF, sz,
                              mem->pointee_is_unsigned);
}

static psx_type_t *type_from_mem(node_mem_t *mem, int force_array, int force_vla) {
  if (!mem) return NULL;

  psx_type_t *type = NULL;
  int looks_like_array_decay =
      mem->is_pointer && !mem->is_tag_pointer && !mem->is_scalar_ptr_member &&
      (mem->is_array_member || mem->type_size > 8) && mem->deref_size > 0 &&
      (mem->type_size % mem->deref_size) == 0 &&
      mem->pointer_qual_levels == 0;

  if (force_array || looks_like_array_decay) {
    int elem_size = mem->deref_size > 0 ? mem->deref_size : mem->base_deref_size;
    if (elem_size <= 0) elem_size = mem->type_size;
    int array_len = (elem_size > 0 && mem->type_size > 0 &&
                     (mem->type_size % elem_size) == 0)
                        ? mem->type_size / elem_size
                        : 0;
    psx_type_t *base = type_new_pointee_base_from_mem(mem);
    type = psx_type_new_array(base, array_len, mem->type_size, elem_size, force_vla);
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
  } else if (mem->is_pointer || mem->is_tag_pointer) {
    psx_type_t *base = type_new_pointee_base_from_mem(mem);
    type = psx_type_new_pointer(base, mem->deref_size);
    psx_type_copy_pointer_metadata(type, (psx_type_t[]){
      {
        .deref_size = mem->deref_size,
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
  } else if (mem->tag_kind == TK_STRUCT || mem->tag_kind == TK_UNION) {
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
    type->tag_kind = mem->tag_kind;
    type->tag_name = mem->tag_name;
    type->tag_len = mem->tag_len;
    type->tag_scope_depth_p1 = mem->tag_scope_depth_p1;
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

static int lvar_is_pointer_like_for_type(lvar_t *var) {
  if (!var) return 0;
  return var->is_array || var->is_vla || var->is_tag_pointer ||
         var->pointer_qual_levels > 0 ||
         (var->size > var->elem_size) ||
         (var->outer_stride > 0 && var->size == 8 && !var->is_array && !var->is_vla) ||
         var->pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         var->pointee_is_void;
}

static void mem_from_lvar(node_mem_t *mem, lvar_t *var) {
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
  mem->is_tag_pointer = var->is_tag_pointer ? 1 : 0;
  mem->is_pointer = lvar_is_pointer_like_for_type(var) ? 1 : 0;
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
  mem->pointee_is_unsigned = var->is_unsigned ? 1 : 0;
  mem->pointee_fp_kind = (unsigned int)var->pointee_fp_kind;
  psx_decl_funcptr_sig_t funcptr_sig = funcptr_sig_from_lvar(var);
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
}

psx_type_t *psx_lvar_get_decl_type(lvar_t *var) {
  return psx_lvar_materialize_decl_type(var);
}

psx_type_t *psx_lvar_materialize_decl_type(lvar_t *var) {
  if (!var) return NULL;
  if (var->decl_type) return var->decl_type;
  node_mem_t mem;
  mem_from_lvar(&mem, var);
  var->decl_type = type_from_mem(&mem, var->is_array || var->is_vla, var->is_vla);
  return var->decl_type;
}

psx_type_t *psx_lvar_refresh_decl_type(lvar_t *var) {
  if (!var) return NULL;
  psx_decl_invalidate_lvar_decl_type(var);
  return psx_lvar_materialize_decl_type(var);
}

static int gvar_is_pointer_like_for_type(const global_var_t *gv) {
  if (!gv) return 0;
  return gv->is_array || gv->is_tag_pointer || gv->pointer_qual_levels > 0 ||
         gv->outer_stride > 0 ||
         gv->ptr_array_pointee_bytes > 0 ||
         gv->pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         (gv->type_size == 8 && gv->deref_size > 0 && gv->deref_size < gv->type_size);
}

static void mem_from_gvar(node_mem_t *mem, global_var_t *gv) {
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
  mem->is_tag_pointer = gv->is_tag_pointer ? 1 : 0;
  mem->is_pointer = gvar_is_pointer_like_for_type(gv) ? 1 : 0;
  mem->is_unsigned = gv->is_unsigned ? 1 : 0;
  mem->is_const_qualified = gv->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = gv->is_volatile_qualified ? 1 : 0;
  mem->is_bool = gv->is_bool ? 1 : 0;
  mem->is_long_double = gv->is_long_double ? 1 : 0;
  mem->pointee_is_bool = gv->elem_is_bool ? 1 : 0;
  mem->pointee_is_unsigned = gv->is_unsigned ? 1 : 0;
  mem->pointee_fp_kind = (unsigned int)gv->pointee_fp_kind;
  if (gv->is_array && gv->fp_kind != TK_FLOAT_KIND_NONE) {
    mem->pointee_fp_kind = (unsigned int)gv->fp_kind;
  }
  psx_decl_funcptr_sig_t funcptr_sig = funcptr_sig_from_gvar(gv);
  node_mem_store_funcptr_signature(mem, &funcptr_sig);
  mem->pointer_qual_levels = gv->pointer_qual_levels;
  mem->inner_deref_size = (short)gv->outer_stride;
  mem->next_deref_size = (short)gv->mid_stride;
  mem->extra_strides_count = gv->extra_strides_count;
  for (int i = 0; i < gv->extra_strides_count && i < 5; i++) {
    mem->extra_strides[i] = gv->extra_strides[i];
  }
  mem->ptr_array_pointee_bytes = gv->ptr_array_pointee_bytes;
}

psx_type_t *psx_gvar_get_decl_type(global_var_t *gv) {
  return psx_gvar_materialize_decl_type(gv);
}

psx_type_t *psx_gvar_materialize_decl_type(global_var_t *gv) {
  if (!gv) return NULL;
  if (gv->decl_type) return gv->decl_type;
  node_mem_t mem;
  mem_from_gvar(&mem, gv);
  gv->decl_type = type_from_mem(&mem, gv->is_array, 0);
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
}

static psx_type_t *type_from_direct_funcall(node_func_t *fn) {
  if (!fn || fn->callee != NULL || !fn->funcname) return NULL;
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(fn->funcname, fn->funcname_len);
  tk_float_kind_t ret_fp_kind = ret.fp_kind;
  if (ret_fp_kind == TK_FLOAT_KIND_NONE) {
    if (ret.token_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (ret.token_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  }
  int size = ret.struct_size > 0 ? ret.struct_size : integer_token_size(ret.token_kind, 4);

  if (!ret.is_pointer) {
    if (ret.tag_kind == TK_STRUCT || ret.tag_kind == TK_UNION)
      return psx_type_new_tag(ret.tag_kind, ret.tag_name, ret.tag_len, 0, ret.struct_size);
    return type_from_scalar_shape(ret.token_kind, ret_fp_kind, size,
                                  ret.is_unsigned, ret.is_complex, 0);
  }

  psx_type_t *base = NULL;
  if (ret.tag_kind == TK_STRUCT || ret.tag_kind == TK_UNION) {
    base = psx_type_new_tag(ret.tag_kind, ret.tag_name, ret.tag_len, 0, ret.struct_size);
  } else {
    base = type_from_scalar_shape(ret.token_kind, ret_fp_kind, size,
                                  ret.is_unsigned, ret.is_complex, 0);
  }
  psx_ret_pointee_array_t ret_array = ret.pointee_array;
  if (ret_array.first_dim <= 0) {
    ret_array.first_dim =
        psx_ctx_get_function_ret_pointee_array_first_dim(fn->funcname, fn->funcname_len);
    ret_array.second_dim =
        psx_ctx_get_function_ret_pointee_array_second_dim(fn->funcname, fn->funcname_len);
  }
  ret_array.elem_size = psx_type_sizeof(base);
  int levels = ret.pointer_levels;
  if (levels <= 0) levels = psx_ctx_get_function_ret_pointer_levels(fn->funcname, fn->funcname_len);
  int deref_size = levels >= 2 ? 8 : psx_type_sizeof(base);
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    int row = psx_ret_pointee_array_row_stride(ret_array);
    if (row > 0) deref_size = row;
  }
  psx_type_t *type = psx_type_new_pointer(base, deref_size);
  type->pointer_qual_levels = levels;
  type->base_deref_size = psx_type_sizeof(base);
  type_apply_pointee_qualifiers(type, ret.pointee_const_qualified,
                                ret.pointee_volatile_qualified);
  if (ret.is_funcptr) {
    type->funcptr_sig = ret.funcptr_sig;
  }
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    type->funcptr_sig.ret_pointee_array = ret_array;
  }
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
    type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
  }
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
    if (cur && (cur->kind == PSX_TYPE_STRUCT || cur->kind == PSX_TYPE_UNION)) {
      tag_type = cur;
      break;
    }
  }
  if (!tag_type && (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION)) {
    tag_type = type;
  }
  if (!tag_type && (type->tag_kind == TK_STRUCT || type->tag_kind == TK_UNION)) {
    tag_type = type;
    ptr = psx_type_is_pointer(type);
  }
  if (!tag_type || (tag_type->tag_kind != TK_STRUCT && tag_type->tag_kind != TK_UNION))
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
  if (node->type && tag_view_from_type(node->type, view)) return 1;
  if (node->kind == ND_FUNCALL &&
      tag_view_from_type(psx_node_get_type(node), view))
    return 1;
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
  if (!funcptr_sig_has_return_shape(callee_sig)) return NULL;

  if (callee_sig.ret_is_void) return type_new_void();
  if (callee_sig.ret_is_complex) {
    int complex_size =
        callee_sig.ret_fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, callee_sig.ret_fp_kind,
                                  complex_size, 0, 1, 0);
  }

  psx_ret_pointee_array_t ret_array = callee_sig.ret_pointee_array;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int ignored_ptr = 0;
  tag_type_from_type(callee_type, &tag_kind, &tag_name, &tag_len, &ignored_ptr, NULL);
  if (!callee_sig.ret_is_data_pointer &&
      (tag_kind == TK_STRUCT || tag_kind == TK_UNION)) {
    int size = callee_type->base ? psx_type_sizeof(callee_type->base) : 0;
    if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_type_new_tag(tag_kind, tag_name, tag_len,
                            callee_type->base ? callee_type->base->tag_scope_depth_p1 : 0,
                            size);
  }

  if (callee_sig.ret_is_data_pointer || psx_ret_pointee_array_has_dims(ret_array)) {
    psx_type_t *base = NULL;
    if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
      int size = callee_type->base ? psx_type_sizeof(callee_type->base) : 0;
      if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      base = psx_type_new_tag(tag_kind, tag_name, tag_len,
                              callee_type->base ? callee_type->base->tag_scope_depth_p1 : 0,
                              size);
    } else if (callee_sig.ret_pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float(callee_sig.ret_pointee_fp_kind,
                                callee_sig.ret_pointee_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    } else {
      int base_size = callee_sig.ret_int_width > 0
                          ? callee_sig.ret_int_width
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
    psx_type_t *type = psx_type_new_pointer(base, deref_size);
    if (callee_type->base) {
      type_apply_pointee_qualifiers(type, callee_type->base->is_const_qualified,
                                    callee_type->base->is_volatile_qualified);
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->funcptr_sig.ret_pointee_array = ret_array;
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
      type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
    }
    type->base_deref_size = psx_type_sizeof(base);
    type->pointer_qual_levels = 1;
    return type;
  }

  if (callee_sig.ret_fp_kind != TK_FLOAT_KIND_NONE) {
    return psx_type_new_float(callee_sig.ret_fp_kind,
                              callee_sig.ret_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  int width = callee_sig.ret_int_width;
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
  if (callee_sig.ret_is_void) return type_new_void();
  if (callee_sig.ret_is_complex) {
    int complex_size = fn->base.fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, (tk_float_kind_t)fn->base.fp_kind,
                                  complex_size, 0, 1, 0);
  }

  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  psx_node_get_tag_type(fn->callee, &tag_kind, &tag_name, &tag_len, NULL);
  if (!callee_sig.ret_is_data_pointer &&
      (tag_kind == TK_STRUCT || tag_kind == TK_UNION)) {
    int size = fn->base.ret_struct_size;
    if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_type_new_tag(tag_kind, tag_name, tag_len,
                            psx_node_get_tag_scope_depth(fn->callee) + 1, size);
  }

  psx_ret_pointee_array_t ret_array = callee_sig.ret_pointee_array;
  int returns_data_pointer =
      callee_sig.ret_is_data_pointer || psx_ret_pointee_array_has_dims(ret_array);
  if (returns_data_pointer) {
    tk_float_kind_t ret_pointee_fp = (tk_float_kind_t)fn->base.fp_kind;
    if (ret_pointee_fp == TK_FLOAT_KIND_NONE)
      ret_pointee_fp = callee_sig.ret_pointee_fp_kind;
    psx_type_t *base = NULL;
    if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
      int size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      base = psx_type_new_tag(tag_kind, tag_name, tag_len,
                              psx_node_get_tag_scope_depth(fn->callee) + 1, size);
    } else if (ret_pointee_fp != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float(ret_pointee_fp,
                                ret_pointee_fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    } else {
      int base_size = callee_sig.ret_int_width > 0
                          ? callee_sig.ret_int_width
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
      type->funcptr_sig.ret_pointee_array = ret_array;
    }
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
      type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
    }
    type->base_deref_size = psx_type_sizeof(base);
    type->pointer_qual_levels = psx_node_pointer_qual_levels(fn->callee) >= 2 ? 1 : 0;
    return type;
  }

  int width = callee_sig.ret_int_width;
  if (width <= 0) {
    if (fn->base.fp_kind == TK_FLOAT_KIND_FLOAT) width = 4;
    else if (fn->base.fp_kind >= TK_FLOAT_KIND_DOUBLE) width = 8;
    else width = 4;
  }
  return type_from_scalar_shape(TK_EOF, (tk_float_kind_t)fn->base.fp_kind, width,
                                fn->base.is_unsigned, 0, width >= 8);
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
  if (is_complex) {
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
    return psx_type_new_float(fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
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
  tk_float_kind_t fp = TK_FLOAT_KIND_NONE;
  if (lhs && lhs->fp_kind > fp) fp = (tk_float_kind_t)lhs->fp_kind;
  if (rhs && rhs->fp_kind > fp) fp = (tk_float_kind_t)rhs->fp_kind;
  return type_usual_arith_result(psx_node_get_type(lhs), psx_node_get_type(rhs), fp,
                                 (lhs && lhs->is_complex) || (rhs && rhs->is_complex));
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
      return type_usual_arith_result(psx_node_get_type(node->lhs),
                                     psx_node_get_type(node->rhs),
                                     (tk_float_kind_t)node->fp_kind,
                                     node->is_complex);
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
      (then_type->kind == PSX_TYPE_STRUCT || then_type->kind == PSX_TYPE_UNION)) {
    return then_type;
  }
  return type_usual_arith_result(then_type, else_type, (tk_float_kind_t)node->fp_kind,
                                 node->is_complex);
}

psx_type_t *psx_node_get_type(node_t *node) {
  if (!node) return NULL;
  if (node->type) return node->type;
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
      return psx_node_get_type(node->rhs);
    case ND_TERNARY:
      return node->type = type_from_ternary_expr(node);
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
      return node->type = type_from_binary_expr(node);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return node->type = psx_node_get_type(node->lhs);
    case ND_FUNCALL: {
      psx_type_t *type = type_from_direct_funcall((node_func_t *)node);
      if (!type) type = type_from_indirect_funcall((node_func_t *)node);
      return node->type = type;
    }
    case ND_FP_TO_INT: {
      int width = ((node_mem_t *)node)->type_size;
      if (width <= 0) width = 4;
      return node->type = psx_type_new_integer(TK_INT, width, node->is_unsigned);
    }
    case ND_INT_TO_FP:
    case ND_FNEG:
    case ND_CREAL:
    case ND_CIMAG:
      return node->type = psx_type_new_float((tk_float_kind_t)node->fp_kind,
                                             node->fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
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
  if (node->type) {
    int value = 0;
    if (node_value_view_from_node_direct(node, NODE_VALUE_TYPE_SIZE, &value) &&
        value > 0)
      return value;
  }
  if (node->kind == ND_FUNCALL) {
    int value = 0;
    if (node_value_view_from_node_direct(node, NODE_VALUE_TYPE_SIZE, &value) &&
        value > 0)
      return value;
  }
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
  switch (node->kind) {
    case ND_COMMA:
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
    case ND_FUNCALL: {
      int value = 0;
      pointer_view_from_node_direct(node, NODE_POINTER_QUAL_LEVELS, &value);
      return value;
    }
    default: {
      int value = 0;
      pointer_view_from_node_direct(node, NODE_POINTER_QUAL_LEVELS, &value);
      return value;
    }
  }
}

int psx_node_base_deref_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_COMMA:
      return psx_node_base_deref_size(node->rhs);
    /* ポインタ算術 `pp + n` / `pp - n`: ポインタ側 (通常 lhs、稀に rhs) の bds を carry。
     * pql と対称で多段ポインタ算術の最終 deref が正しい load 幅を使えるようにする。 */
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_base_deref_size(node->lhs);
      if (l > 0) return l;
      return psx_node_base_deref_size(node->rhs);
    }
    case ND_FUNCALL: {
      int value = 0;
      pointer_view_from_node_direct(node, NODE_POINTER_BASE_DEREF_SIZE, &value);
      return value;
    }
    default: {
      int value = 0;
      pointer_view_from_node_direct(node, NODE_POINTER_BASE_DEREF_SIZE, &value);
      return value;
    }
  }
}

int psx_node_ptr_array_pointee_bytes(node_t *node) {
  if (!node) return 0;
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
      int value = 0;
      if (pointer_view_from_node_direct(node, NODE_POINTER_PTR_ARRAY_POINTEE_BYTES,
                                        &value))
        return value;
      return 0;
    }
    default: {
      int value = 0;
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
      return as_mem(node);
    default:
      return NULL;
  }
}

static int type_is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

static tk_float_kind_t type_deep_pointee_fp_kind(const psx_type_t *type) {
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
  if (!type_is_pointer_view_type(type) || !type->base) return 0;
  const psx_type_t *base = type->base;
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
      return (mem->pointee_is_unsigned || mem->is_unsigned) ? 1 : 0;
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

static int pointer_view_from_type(const psx_type_t *type, node_pointer_view_field_t field,
                                  int *value) {
  if (!type_is_pointer_view_type(type)) return 0;
  switch (field) {
    case NODE_POINTER_QUAL_LEVELS:
      if (value) *value = type->pointer_qual_levels;
      return 1;
    case NODE_POINTER_BASE_DEREF_SIZE:
      if (type->base_deref_size <= 0) return 0;
      if (value) *value = type->base_deref_size;
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
  if (mem->type_size <= mem->deref_size) return 0;
  return mem->is_pointer || mem->inner_deref_size > 0 ||
         mem->next_deref_size > 0 || mem->extra_strides_count > 0;
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
    int is_ptr = type && psx_type_is_pointer(type);
    int mem_is_ptr = 0;
    int has_mem = node_value_view_from_mem(mem, field, &mem_is_ptr);
    if (value) *value = (is_ptr || mem_is_ptr) ? 1 : 0;
    return type || has_mem;
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

unsigned short psx_node_funcptr_param_fp_mask(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).param_fp_mask;
}

unsigned short psx_node_funcptr_param_int_mask(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).param_int_mask;
}

int psx_node_funcptr_returns_void(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).ret_is_void ? 1 : 0;
}

int psx_node_funcptr_returns_complex(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node, 1).ret_is_complex ? 1 : 0;
}

int psx_node_funcptr_returns_pointee_array(node_t *node) {
  if (!node) return 0;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_node(node, 1);
  return psx_ret_pointee_array_has_dims(sig.ret_pointee_array) ? 1 : 0;
}

tk_float_kind_t psx_node_funcptr_ret_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  return funcptr_sig_from_node(node, 1).ret_fp_kind;
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

void psx_node_init_gvar_ref_metadata(node_mem_t *mem, const global_var_t *gv) {
  if (!mem) return;
  *mem = (node_mem_t){0};
  mem->base.kind = ND_GVAR;
  if (!gv) return;
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
  mem->is_tag_pointer = gv->is_tag_pointer ? 1 : 0;
  mem->is_pointer = gvar_is_pointer_like_for_type(gv) ? 1 : 0;
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
  mem->is_long_double = gv->is_long_double ? 1 : 0;
}

void psx_node_init_gvar_array_base_metadata(node_mem_t *mem, const global_var_t *gv) {
  if (!mem) return;
  *mem = (node_mem_t){0};
  mem->base.kind = ND_GVAR;
  if (!gv) return;
  mem->type_size = (short)gv->type_size;
  mem->deref_size = gv->deref_size;
  mem->tag_kind = gv->tag_kind;
  mem->tag_name = gv->tag_name;
  mem->tag_len = gv->tag_len;
  mem->tag_scope_depth_p1 = gv->tag_scope_depth_p1;
  mem->is_const_qualified = gv->is_const_qualified ? 1 : 0;
  mem->is_volatile_qualified = gv->is_volatile_qualified ? 1 : 0;
}

void psx_node_init_static_local_gvar_ref_metadata(node_mem_t *mem, const lvar_t *var,
                                                  int type_size) {
  if (!mem) return;
  *mem = (node_mem_t){0};
  mem->base.kind = ND_GVAR;
  if (!var) return;
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

void psx_node_init_lvar_array_addr_metadata(node_mem_t *addr, const lvar_t *var,
                                            int is_tag_pointer) {
  if (!addr || !var) return;
  init_lvar_array_addr_strides(addr, var);
  addr->is_pointer = 1;
  addr->pointee_fp_kind = var->pointee_fp_kind != TK_FLOAT_KIND_NONE
                             ? (unsigned int)var->pointee_fp_kind
                             : (unsigned int)var->fp_kind;
  addr->pointee_is_bool = var->is_bool ? 1 : 0;
  addr->pointee_is_unsigned = var->is_unsigned ? 1 : 0;
  psx_node_copy_funcptr_metadata_from_lvar(addr, var);
  addr->tag_kind = var->tag_kind;
  addr->tag_name = var->tag_name;
  addr->tag_len = var->tag_len;
  addr->tag_scope_depth_p1 = var->tag_scope_depth_p1;
  addr->is_tag_pointer = is_tag_pointer ? 1 : 0;
  addr->is_const_qualified = var->is_const_qualified ? 1 : 0;
  addr->is_volatile_qualified = var->is_volatile_qualified ? 1 : 0;
}

void psx_node_init_gvar_array_addr_metadata(node_mem_t *addr, const global_var_t *gv) {
  if (!addr || !gv) return;
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
  addr->pointee_is_unsigned = gv->is_unsigned ? 1 : 0;
  psx_node_copy_funcptr_metadata_from_gvar(addr, gv);
  if (gv->pointee_elem_size > 0 && gv->tag_kind == TK_EOF) {
    addr->pointee_is_scalar_ptr = 1;
    if (addr->base_deref_size == 0) addr->base_deref_size = (short)gv->pointee_elem_size;
  }
  if (gv->tag_kind != TK_EOF && gv->is_tag_pointer) {
    if (addr->base_deref_size == 0) addr->base_deref_size = (short)gv->deref_size;
    if (addr->pointer_qual_levels == 0) addr->pointer_qual_levels = 1;
  }
}

void psx_node_init_compound_lvar_array_addr_metadata(node_mem_t *addr, const lvar_t *var,
                                                     token_kind_t tag_kind, char *tag_name,
                                                     int tag_len, int array_size) {
  if (!addr || !var) return;
  addr->tag_kind = tag_kind;
  addr->tag_name = tag_name;
  addr->tag_len = tag_len;
  init_lvar_array_addr_strides(addr, var);
  addr->is_pointer = 1;
  if (addr->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
  addr->compound_literal_array_size = array_size;
}

void psx_node_init_compound_gvar_array_addr_metadata(node_mem_t *addr, const global_var_t *gv,
                                                     int ptr_array_pointee_bytes,
                                                     int pointer_elem_size, int array_size) {
  if (!addr || !gv) return;
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
}

unsigned int psx_node_pointer_const_qual_mask(node_t *node) {
  if (!node) return 0;
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
    default: {
      int value = 0;
      pointer_view_from_node_direct(node, NODE_POINTER_CONST_MASK, &value);
      return (unsigned int)value;
    }
  }
}

unsigned int psx_node_pointer_volatile_qual_mask(node_t *node) {
  if (!node) return 0;
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
    default: {
      int value = 0;
      pointer_view_from_node_direct(node, NODE_POINTER_VOLATILE_MASK, &value);
      return (unsigned int)value;
    }
  }
}

int psx_node_pointee_is_unsigned(node_t *node) {
  if (!node) return 0;
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

int psx_node_pointee_is_bool(node_t *node) {
  if (!node) return 0;
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
  switch (node->kind) {
    case ND_NUM:
      return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_LONG) ||
             ((node_num_t *)node)->int_is_long_long ? 1 : 0;
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_is_long_long_type(node->rhs);
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      return psx_node_is_long_long_type(ctrl->base.rhs) ||
             psx_node_is_long_long_type(ctrl->els);
    }
    default: {
      return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_LONG);
    }
  }
}

int psx_node_is_plain_char_type(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_NUM)
    return scalar_flag_from_node_direct(node, NODE_SCALAR_PLAIN_CHAR) ||
           ((node_num_t *)node)->int_is_plain_char ? 1 : 0;
  if (node->kind == ND_COMMA || node->kind == ND_STMT_EXPR)
    return psx_node_is_plain_char_type(node->rhs);
  if (node->kind == ND_TERNARY) {
    node_ctrl_t *ctrl = (node_ctrl_t *)node;
    return psx_node_is_plain_char_type(ctrl->base.rhs) ||
           psx_node_is_plain_char_type(ctrl->els);
  }
  return scalar_flag_from_node_direct(node, NODE_SCALAR_PLAIN_CHAR);
}

int psx_node_is_long_double_type(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_COMMA || node->kind == ND_STMT_EXPR)
    return psx_node_is_long_double_type(node->rhs);
  if (node->kind == ND_TERNARY) {
    node_ctrl_t *ctrl = (node_ctrl_t *)node;
    return psx_node_is_long_double_type(ctrl->base.rhs) ||
           psx_node_is_long_double_type(ctrl->els);
  }
  return scalar_flag_from_node_direct(node, NODE_SCALAR_LONG_DOUBLE);
}

tk_float_kind_t psx_node_pointee_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  switch (node->kind) {
    case ND_COMMA:
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
    case ND_FUNCALL: {
      int value = TK_FLOAT_KIND_NONE;
      if (pointer_view_from_node_direct(node, NODE_POINTER_POINTEE_FP_KIND, &value) &&
          value != TK_FLOAT_KIND_NONE)
        return (tk_float_kind_t)value;
      return TK_FLOAT_KIND_NONE;
    }
    default: {
      int value = TK_FLOAT_KIND_NONE;
      pointer_view_from_node_direct(node, NODE_POINTER_POINTEE_FP_KIND, &value);
      return (tk_float_kind_t)value;
    }
  }
}

/* pointer-to-VLA (`int (*p)[m]`) の行ストライドスロット (実行時値) のフレームオフセット。
 * 無ければ 0。ポインタ算術 (`p + 1`) のスケールに使う。ND_ADD/SUB は被演算子を辿る。 */
int psx_node_vla_row_stride_frame_off(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_LVAR: {
      int value = 0;
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
      int value = 0;
      vla_view_from_node_direct(node, NODE_VLA_ROW_STRIDE_FRAME_OFF, &value);
      return value;
    }
  }
}

static int node_vla_strides_remaining(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_LVAR: {
      int value = 0;
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
      int value = 0;
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
  psx_ret_pointee_array_t ret_array = type->funcptr_sig.ret_pointee_array;
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
  if (node_pointer_stride_from_type(type, inner_stride, next_stride,
                                    extra_strides, extra_strides_count)) {
    return 1;
  }
  if (node_pointer_stride_from_funcall_return(node, type, inner_stride, next_stride))
    return 1;
  if (had_direct_type) return 0;
  return node_pointer_stride_from_mem(node_mem_view(node), inner_stride, next_stride,
                                      extra_strides, extra_strides_count);
}

int psx_node_pointer_stride_metadata(node_t *node, int *inner_stride,
                                     int *next_stride, int *extra_strides,
                                     int *extra_strides_count) {
  node_pointer_stride_clear(inner_stride, next_stride, extra_strides, extra_strides_count);
  if (!node) return 0;
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
  if (lhs && lhs->fp_kind) node->fp_kind = lhs->fp_kind;
  if (rhs && rhs->fp_kind > node->fp_kind) node->fp_kind = rhs->fp_kind;

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
  if ((lhs && lhs->is_complex) || (rhs && rhs->is_complex)) {
    node->is_complex = 1;
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
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_fp_slot_at(
      offset, type_size, owner ? owner->fp_kind : TK_FLOAT_KIND_NONE);
  node->var = owner;
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

node_t *psx_node_new_lvar_for(lvar_t *var) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar(var ? var->offset : 0);
  if (var) mem_from_lvar(&node->mem, var);
  node->var = var;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_typed_for(lvar_t *var, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_for(var);
  node->mem.type_size = type_size;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_object_ref_for(lvar_t *var) {
  return psx_node_new_lvar_typed_for(var, var ? var->size : 0);
}

node_t *psx_node_new_lvar_expr_ref_for(lvar_t *var, int is_pointer) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed_for(
      var, is_pointer ? 8 : (var ? var->elem_size : 0));
  if (var) node->mem.deref_size = var->elem_size;
  node->mem.is_pointer = is_pointer ? 1 : 0;
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

node_t *psx_node_new_lvar_identifier_ref_for(lvar_t *var) {
  if (var && var->is_static_local && var->static_global_name) {
    int sz = var->size > 0 ? var->size : var->elem_size;
    return psx_node_new_static_local_gvar_for(var, sz);
  }

  int is_pointer = lvar_is_identifier_pointer_like(var);
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
  return (node_t *)node;
}

node_t *psx_node_new_param_lvar_for(lvar_t *var, int abi_type_size,
                                    int is_unsigned, tk_float_kind_t abi_fp_kind,
                                    int is_complex) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed_for(var, abi_type_size);
  node->mem.base.is_unsigned = is_unsigned ? 1 : 0;
  node->mem.is_unsigned = is_unsigned ? 1 : 0;

  if (lvar_is_identifier_pointer_like(var)) {
    node->mem.is_pointer = 1;
    node->mem.type_size = 8;
    node->mem.deref_size = (var->outer_stride > 0) ? (short)var->outer_stride
                           : (var->vla_row_stride_frame_off ? 0 : (short)var->elem_size);
  }

  if (abi_fp_kind != TK_FLOAT_KIND_NONE) node->mem.base.fp_kind = abi_fp_kind;
  if (is_complex) {
    node->mem.base.is_complex = 1;
    node->mem.is_complex = 1;
  }
  return (node_t *)node;
}

node_t *psx_node_new_array_elem_lvar_for(lvar_t *var, int idx) {
  int elem_size = var ? var->elem_size : 0;
  int offset = var ? var->offset + idx * elem_size : 0;
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar_typed(offset, elem_size);
  node->var = var;
  if (var) {
    node->mem.base.fp_kind = var->fp_kind;
    node->mem.base.is_unsigned = var->is_unsigned ? 1 : 0;
    node->mem.is_unsigned = var->is_unsigned ? 1 : 0;
    node->mem.is_bool = var->is_bool ? 1 : 0;
    node->mem.tag_kind = var->tag_kind;
    node->mem.tag_name = var->tag_name;
    node->mem.tag_len = var->tag_len;
    node->mem.tag_scope_depth_p1 = var->tag_scope_depth_p1;
    node->mem.is_tag_pointer = var->is_tag_pointer ? 1 : 0;
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
  return annotate_explicit_type((node_t *)wrap, cast_type);
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
  if (type_kind == TK_VOID) {
    wrap->pointee_is_void = 1;
  } else if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
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

static psx_decl_funcptr_sig_t funcptr_sig_for_deref_result(psx_decl_funcptr_sig_t sig,
                                                           const psx_type_t *base,
                                                           int pointer_levels) {
  if (!base || pointer_levels > 1 || !sig.ret_is_data_pointer ||
      psx_ret_pointee_array_has_dims(sig.ret_pointee_array)) {
    return sig;
  }

  sig.ret_is_data_pointer = 0;
  sig.ret_is_void = 0;
  sig.ret_is_complex = 0;
  sig.ret_int_width = 0;
  sig.ret_fp_kind = TK_FLOAT_KIND_NONE;
  sig.ret_pointee_fp_kind = TK_FLOAT_KIND_NONE;

  switch (base->kind) {
    case PSX_TYPE_VOID:
      sig.ret_is_void = 1;
      break;
    case PSX_TYPE_COMPLEX:
      sig.ret_is_complex = 1;
      sig.ret_fp_kind =
          base->fp_kind != TK_FLOAT_KIND_NONE ? base->fp_kind : TK_FLOAT_KIND_DOUBLE;
      break;
    case PSX_TYPE_FLOAT:
      sig.ret_fp_kind = base->fp_kind;
      break;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER: {
      int width = psx_type_sizeof(base);
      sig.ret_int_width = (unsigned char)(width >= 8 ? 8 : 4);
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
  if (!type_is_pointer_view_type(type->base) && type->pointer_qual_levels <= 1) {
    int elem_size = type->base_deref_size > 0 ? type->base_deref_size
                                              : psx_type_sizeof(type->base);
    if (elem_size > 0 && type->deref_size > elem_size) {
      int array_len = type->deref_size / elem_size;
      psx_type_t *array =
          psx_type_new_array(type->base, array_len, type->deref_size, elem_size, 0);
      array->base_deref_size = elem_size;
      array->pointee_fp_kind = type->pointee_fp_kind;
      array->ptr_array_pointee_bytes = type->ptr_array_pointee_bytes;
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
        funcptr_sig_for_deref_result(type->funcptr_sig, type->base,
                                     result->pointer_qual_levels);
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
  node->funcptr_sig = type->funcptr_sig;
  node->ptr_array_pointee_bytes = type->ptr_array_pointee_bytes;
  node->inner_deref_size = (short)type->outer_stride;
  node->next_deref_size = (short)type->mid_stride;
  node->extra_strides_count = type->extra_strides_count;
  for (int i = 0; i < 5; i++) node->extra_strides[i] = type->extra_strides[i];
}

node_t *psx_node_new_gvar_array_addr_for(global_var_t *gv) {
  node_mem_t *addr = new_addr_node(psx_node_new_gvar_array_base_for(gv));
  psx_node_init_gvar_array_addr_metadata(addr, gv);
  return (node_t *)addr;
}

node_t *psx_node_new_static_local_array_addr_for(lvar_t *var, int gvar_type_size) {
  node_mem_t *addr = new_addr_node(psx_node_new_static_local_gvar_for(var, gvar_type_size));
  psx_node_init_lvar_array_addr_metadata(addr, var, 0);
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
                                                  int array_size) {
  node_mem_t *addr = new_addr_node(psx_node_new_gvar_for(gv));
  psx_node_init_compound_gvar_array_addr_metadata(addr, gv, ptr_array_pointee_bytes,
                                                  pointer_elem_size, array_size);
  return (node_t *)addr;
}

node_t *psx_node_new_compound_lvar_array_addr_for(lvar_t *var,
                                                  token_kind_t tag_kind,
                                                  char *tag_name, int tag_len,
                                                  int array_size) {
  node_mem_t *addr = new_addr_node(psx_node_new_lvar_for(var));
  psx_node_init_compound_lvar_array_addr_metadata(addr, var, tag_kind, tag_name,
                                                  tag_len, array_size);
  return (node_t *)addr;
}

node_t *psx_node_new_addr_value_for(node_t *operand) {
  node_mem_t *addr = new_addr_node(operand);
  addr->base.type = type_from_address_operand(operand);
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

static int member_ptr_array_pointee_elem_size(const tag_member_info_t *info) {
  if (!info || info->ptr_array_pointee_bytes <= 0 || info->arr_ndim <= 0) return 0;
  int count = 1;
  for (int i = 0; i < info->arr_ndim && i < 8; i++) {
    if (info->arr_dims[i] <= 0) return 0;
    count *= info->arr_dims[i];
  }
  if (count <= 0 || (info->ptr_array_pointee_bytes % count) != 0) return 0;
  return info->ptr_array_pointee_bytes / count;
}

node_t *psx_node_new_tag_member_deref_for(node_t *addr_base, node_t *base,
                                          const tag_member_info_t *info) {
  if (!info) return NULL;
  node_t *addr = psx_node_new_binary(ND_ADD, addr_base, psx_node_new_num(info->offset));
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  int mem_size = info->type_size;
  int mem_array_len = info->array_len;
  int mem_is_ptr = info->is_tag_pointer;
  deref->type_size = mem_size ? mem_size : 8;
  deref->deref_size = info->deref_size;
  if (mem_array_len > 0 && mem_size > 0) {
    deref->type_size = mem_size * mem_array_len;
    deref->deref_size = mem_size;
    deref->is_pointer = 1;
    deref->is_array_member = 1;
    if (info->outer_stride > 0) {
      deref->deref_size = info->outer_stride;
      deref->inner_deref_size = (short)mem_size;
      if (info->mid_stride > 0) {
        deref->inner_deref_size = (short)info->mid_stride;
        deref->next_deref_size = (short)mem_size;
      }
    }
    if (mem_is_ptr) {
      deref->is_tag_pointer = 0;
      deref->pointer_qual_levels = 1;
      deref->base_deref_size = (short)info->deref_size;
      if (info->ptr_array_pointee_bytes > 0) {
        deref->ptr_array_pointee_bytes = info->ptr_array_pointee_bytes;
        int ptr_arr_elem = member_ptr_array_pointee_elem_size(info);
        if (ptr_arr_elem > 0) deref->base_deref_size = (short)ptr_arr_elem;
      }
    }
  } else if (mem_is_ptr && mem_size > 0 && info->outer_stride > 0) {
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
    deref->deref_size = (short)info->outer_stride;
    if (info->mid_stride > 0) {
      deref->inner_deref_size = (short)info->mid_stride;
      deref->next_deref_size = (short)info->deref_size;
    } else {
      deref->inner_deref_size = (short)info->deref_size;
    }
  } else if (mem_is_ptr && mem_size > 0) {
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
    if ((info->tag_kind == TK_STRUCT || info->tag_kind == TK_UNION) &&
        info->tag_name) {
      int pointee_size = psx_ctx_get_tag_size(info->tag_kind, info->tag_name,
                                              info->tag_len);
      deref->pointer_qual_levels = info->pointer_qual_levels > 0
                                      ? info->pointer_qual_levels
                                      : 1;
      deref->base_deref_size = (short)(pointee_size > 0 ? pointee_size : 8);
    }
    if (info->ptr_array_pointee_bytes > 0) {
      deref->ptr_array_pointee_bytes = info->ptr_array_pointee_bytes;
      int ptr_arr_elem = member_ptr_array_pointee_elem_size(info);
      deref->base_deref_size = (short)(ptr_arr_elem > 0 ? ptr_arr_elem : info->deref_size);
      deref->deref_size = 8;
    }
  }
  deref->tag_kind = info->tag_kind;
  deref->tag_name = info->tag_name;
  deref->tag_len = info->tag_len;
  deref->is_tag_pointer = mem_is_ptr;
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
  psx_node_copy_funcptr_metadata_from_tag_member(deref, info);
  if (info->fp_kind != TK_FLOAT_KIND_NONE) {
    if (mem_array_len > 0 && mem_size > 0)      deref->pointee_fp_kind = info->fp_kind;
    else if (mem_is_ptr && mem_size > 0)        deref->pointee_fp_kind = info->fp_kind;
    else                                       deref->base.fp_kind = info->fp_kind;
  }
  if (info->is_bool) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_bool = 1;
    else                                  deref->is_bool = 1;
  }
  if (info->is_unsigned) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_unsigned = 1;
    else                                  deref->is_unsigned = 1;
  }
  deref->base.type = type_from_mem(deref, mem_array_len > 0, 0);
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
      node->base.type = type_from_mem(node, 1, 0);
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
    node->ptr_array_pointee_bytes = operand_ptr_array_pointee_bytes;
    int operand_base_deref_size = psx_node_base_deref_size(operand);
    if (node->base_deref_size == 0 && operand_base_deref_size > 0) {
      node->base_deref_size = (short)operand_base_deref_size;
    }
    if (!row_deref_normalized && operand_base_deref_size > 0 &&
        operand_ptr_array_pointee_bytes > operand_base_deref_size &&
        psx_node_pointer_qual_levels(operand) <= 1) {
      node->type_size = operand_ptr_array_pointee_bytes;
      node->deref_size = (short)operand_base_deref_size;
      node->inner_deref_size = 0;
      node->is_tag_pointer = 0;
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
  sync_unary_deref_mem_from_pointer_type(node);

  {
    node_t *probe = operand;
    while (probe && probe->kind == ND_ADD) probe = probe->lhs;
    if (probe && probe->kind == ND_LVAR) {
      lvar_t *src = psx_node_lvar_symbol(probe);
      if (src && src->outer_stride > 0 && src->mid_stride == 0 && !src->is_array) {
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
        if (src->ptr_array_pointee_bytes > 0) {
          node->ptr_array_pointee_bytes = src->ptr_array_pointee_bytes;
          if (node->base_deref_size == 0) node->base_deref_size = (short)src->elem_size;
        }
      }
    } else if (probe && probe->kind == ND_FUNCALL) {
      int inner = 0;
      int next = 0;
      psx_type_t *func_type = psx_node_get_type(probe);
      psx_ret_pointee_array_t func_ret_array =
          func_type ? func_type->funcptr_sig.ret_pointee_array : (psx_ret_pointee_array_t){0};
      if (func_type && psx_ret_pointee_array_has_dims(func_ret_array) &&
          psx_node_pointer_stride_metadata(probe, &inner, &next, NULL, NULL)) {
        if (inner > 0) {
          node->deref_size = (short)inner;
          if (next > 0) node->inner_deref_size = (short)next;
        }
        if (func_type->base &&
            (func_type->base->kind == PSX_TYPE_STRUCT ||
             func_type->base->kind == PSX_TYPE_UNION)) {
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
    if (src && src->outer_stride > 0 && src->mid_stride > 0) {
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
      }
    }
  }

  if (node->deref_size > 0 && node->base.fp_kind != TK_FLOAT_KIND_NONE &&
      node->pointee_fp_kind == TK_FLOAT_KIND_NONE) {
    node->pointee_fp_kind = node->base.fp_kind;
  }
  if (row_deref_normalized) {
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
  if (base_type) base_ret_array = base_type->funcptr_sig.ret_pointee_array;
  int base_is_ret_pointee_array =
      psx_ret_pointee_array_has_dims(base_ret_array) ? 1 : 0;
  int base_is_unary_ptr_array_deref =
      base && base->kind == ND_DEREF && base->lhs &&
      psx_node_ptr_array_pointee_bytes(base->lhs) > 0;

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
      (inner_deref_size > 0 && elem_size > inner_deref_size) ||
      vla_subscript_keeps_row;
  int deref_from_pointer_to_array = 0;
  if (base->kind == ND_DEREF && base->lhs) {
    node_t *probe = base->lhs;
    while (probe && probe->kind == ND_ADD) probe = probe->lhs;
    if (probe && probe->kind == ND_LVAR) {
      lvar_t *src = psx_node_lvar_symbol(probe);
      if (src && src->outer_stride > 0 && !src->is_array) {
        deref_from_pointer_to_array = 1;
      }
    }
  }
  if (pql >= 1 && bds > 0 && subscript_is_intermediate_row) {
    deref->pointer_qual_levels = vla_subscript_keeps_row ? 0 : pql;
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

  {
    if (base_ptr_array_pointee_bytes > 0 && bds > 0) {
      if (subscript_is_intermediate_row) {
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

  {
    node_mem_t *base_mem = base && (base->kind == ND_ADDR || base->kind == ND_LVAR ||
                                    base->kind == ND_GVAR || base->kind == ND_DEREF)
                              ? (node_mem_t *)base : NULL;
    if (psx_node_pointee_is_bool(base)) {
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
  if (!subscript_is_intermediate_row) {
    deref->base.type = type_from_mem(deref, 0, 0);
  }
  return (node_t *)deref;
}

node_t *psx_node_new_byref_param_deref_for(lvar_t *var) {
  node_t *ptr_lvar = psx_node_new_lvar_typed_for(var, 8);
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = ptr_lvar;
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

node_t *psx_node_new_tag_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                             const tag_member_info_t *info) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_member_lvar_ref_for(
      owner, member_offset, info ? info->type_size : 0,
      info ? info->tag_kind : TK_EOF,
      info ? info->tag_name : NULL,
      info ? info->tag_len : 0,
      info ? info->is_tag_pointer : 0);
  if (info && !info->is_tag_pointer && info->fp_kind != TK_FLOAT_KIND_NONE) {
    node->mem.base.fp_kind = info->fp_kind;
  }
  if (info && info->is_bool) {
    node->mem.is_bool = 1;
  }
  if (info && info->is_unsigned) {
    node->mem.base.is_unsigned = 1;
    node->mem.is_unsigned = 1;
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
  if (lhs->type) return type && type->kind == PSX_TYPE_BOOL;
  if (type && type->kind == PSX_TYPE_BOOL) return 1;
  node_mem_t *mem = node_mem_view(lhs);
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
  node->base.fp_kind = lhs ? lhs->fp_kind : TK_FLOAT_KIND_NONE;
  if (lhs && lhs->is_complex) {
    node->base.is_complex = 1;
  }
  if (lhs && lhs->is_atomic) {
    node->base.is_atomic = 1;
  }
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
