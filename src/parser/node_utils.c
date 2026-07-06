#include "node_utils.h"
#include "decl.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "arena.h"
#include "diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
static inline token_t *curtok(void) { return tk_get_current_token(); }
static int node_is_long_long(node_t *node);
static psx_type_t *type_new_void(void);

static int is_mem_node_kind(node_kind_t kind) {
  return kind == ND_LVAR || kind == ND_GVAR || kind == ND_DEREF ||
         kind == ND_ASSIGN || kind == ND_ADDR || kind == ND_STRING ||
         kind == ND_CAST;
}

static int node_kind_has_explicit_cast_type(node_kind_t kind) {
  return kind == ND_CAST || kind == ND_FP_TO_INT || kind == ND_INT_TO_FP ||
         kind == ND_FNEG || kind == ND_CREAL || kind == ND_CIMAG;
}

static psx_type_t *node_explicit_cast_type(node_t *node) {
  if (!node || !node->type || !node_kind_has_explicit_cast_type(node->kind)) return NULL;
  return node->type;
}

static void type_copy_funcptr_metadata(psx_type_t *type, const node_mem_t *mem) {
  if (!type || !mem) return;
  type->funcptr_nargs_fixed = mem->funcptr_nargs_fixed;
  type->funcptr_param_fp_mask = mem->funcptr_param_fp_mask;
  type->funcptr_param_int_mask = mem->funcptr_param_int_mask;
  type->funcptr_ret_int_width = mem->funcptr_ret_int_width;
  type->funcptr_ret_pointee_array_first_dim = mem->funcptr_ret_pointee_array_first_dim;
  type->funcptr_ret_pointee_array_second_dim = mem->funcptr_ret_pointee_array_second_dim;
  type->funcptr_ret_pointee_array_elem_size = mem->funcptr_ret_pointee_array_elem_size;
  type->returns_void = mem->funcptr_ret_is_void ? 1 : 0;
  type->returns_data_pointer = mem->funcptr_ret_is_data_pointer ? 1 : 0;
  type->returns_complex = mem->funcptr_ret_is_complex ? 1 : 0;
  type->is_variadic_func = mem->is_variadic_funcptr ? 1 : 0;
}

static psx_type_t *type_from_mem(node_mem_t *mem) {
  if (!mem) return NULL;

  psx_type_t *type = NULL;
  int looks_like_array_decay =
      mem->is_pointer && !mem->is_tag_pointer && !mem->is_scalar_ptr_member &&
      mem->type_size > 8 && mem->deref_size > 0 &&
      (mem->type_size % mem->deref_size) == 0 &&
      mem->pointer_qual_levels == 0;

  if (looks_like_array_decay) {
    psx_type_t *base = NULL;
    if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind, mem->deref_size);
    } else {
      base = psx_type_new_integer(mem->pointee_is_bool ? TK_BOOL : TK_EOF,
                                  mem->deref_size, mem->pointee_is_unsigned);
    }
    type = psx_type_new_array(base, mem->type_size / mem->deref_size,
                              mem->type_size, mem->deref_size, 0);
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
    psx_type_t *base = NULL;
    if (mem->pointee_is_void) {
      base = type_new_void();
    } else if (mem->tag_kind == TK_STRUCT || mem->tag_kind == TK_UNION) {
      base = psx_type_new_tag(mem->tag_kind, mem->tag_name, mem->tag_len,
                              mem->tag_scope_depth_p1, mem->deref_size);
    } else if (mem->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      int sz = mem->deref_size > 0 ? mem->deref_size : 8;
      base = psx_type_new_float((tk_float_kind_t)mem->pointee_fp_kind, sz);
    } else {
      int sz = mem->deref_size > 0 ? mem->deref_size : mem->base_deref_size;
      if (sz <= 0 || sz > 8) sz = 4;
      base = psx_type_new_integer(mem->pointee_is_bool ? TK_BOOL : TK_EOF, sz,
                                  mem->pointee_is_unsigned);
    }
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
  mem->is_variadic_funcptr = var->is_variadic_funcptr ? 1 : 0;
  mem->funcptr_param_fp_mask = var->funcptr_param_fp_mask;
  mem->funcptr_param_int_mask = var->funcptr_param_int_mask;
  mem->funcptr_ret_int_width = var->funcptr_ret_int_width;
  mem->funcptr_nargs_fixed = var->funcptr_nargs_fixed;
  mem->funcptr_ret_pointee_array_first_dim = var->funcptr_ret_pointee_array_first_dim;
  mem->funcptr_ret_pointee_array_second_dim = var->funcptr_ret_pointee_array_second_dim;
  mem->funcptr_ret_pointee_array_elem_size = var->funcptr_ret_pointee_array_elem_size;
  mem->funcptr_ret_is_void = var->funcptr_ret_is_void ? 1 : 0;
  mem->funcptr_ret_is_data_pointer = var->funcptr_ret_is_data_pointer ? 1 : 0;
  mem->funcptr_ret_is_complex = var->funcptr_ret_is_complex ? 1 : 0;
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
  if (!var) return NULL;
  if (var->decl_type) return var->decl_type;
  node_mem_t mem;
  mem_from_lvar(&mem, var);
  return type_from_mem(&mem);
}

psx_type_t *psx_lvar_materialize_decl_type(lvar_t *var) {
  if (!var) return NULL;
  node_mem_t mem;
  mem_from_lvar(&mem, var);
  var->decl_type = type_from_mem(&mem);
  return var->decl_type;
}

static int gvar_is_pointer_like_for_type(global_var_t *gv) {
  if (!gv) return 0;
  return gv->is_array || gv->is_tag_pointer || gv->pointer_qual_levels > 0 ||
         gv->outer_stride > 0 ||
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
  mem->is_variadic_funcptr = gv->is_variadic_funcptr ? 1 : 0;
  mem->funcptr_param_fp_mask = gv->funcptr_param_fp_mask;
  mem->funcptr_param_int_mask = gv->funcptr_param_int_mask;
  mem->funcptr_ret_int_width = gv->funcptr_ret_int_width;
  mem->funcptr_nargs_fixed = gv->funcptr_nargs_fixed;
  mem->funcptr_ret_is_void = gv->funcptr_ret_is_void ? 1 : 0;
  mem->funcptr_ret_is_data_pointer = gv->funcptr_ret_is_data_pointer ? 1 : 0;
  mem->funcptr_ret_is_complex = gv->funcptr_ret_is_complex ? 1 : 0;
  mem->funcptr_ret_pointee_array_first_dim = gv->funcptr_ret_pointee_array_first_dim;
  mem->funcptr_ret_pointee_array_second_dim = gv->funcptr_ret_pointee_array_second_dim;
  mem->funcptr_ret_pointee_array_elem_size = gv->funcptr_ret_pointee_array_elem_size;
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
  if (!gv) return NULL;
  if (gv->decl_type) return gv->decl_type;
  node_mem_t mem;
  mem_from_gvar(&mem, gv);
  return type_from_mem(&mem);
}

psx_type_t *psx_gvar_materialize_decl_type(global_var_t *gv) {
  if (!gv) return NULL;
  node_mem_t mem;
  mem_from_gvar(&mem, gv);
  gv->decl_type = type_from_mem(&mem);
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
    type->returns_void = ret.funcptr_ret_is_void ? 1 : 0;
    type->returns_data_pointer = ret.funcptr_ret_is_pointer ? 1 : 0;
    type->returns_complex = ret.funcptr_ret_is_complex ? 1 : 0;
    type->funcptr_ret_int_width = (unsigned char)ret.funcptr_ret_int_width;
  }
  PSX_RET_POINTEE_ARRAY_STORE_SHORT_FIELDS_IF_PRESENT(type, ret_array);
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
    type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
  }
  return type;
}

static int tag_type_from_type(psx_type_t *type, token_kind_t *tag_kind, char **tag_name,
                              int *tag_len, int *is_tag_pointer) {
  if (!type) return 0;
  psx_type_t *tag_type = NULL;
  int ptr = 0;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) {
    if (type->base && (type->base->kind == PSX_TYPE_STRUCT ||
                       type->base->kind == PSX_TYPE_UNION)) {
      tag_type = type->base;
      ptr = 1;
    }
  } else if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION) {
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
  return 1;
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
  if (!fn || !fn->callee || fn->callee->kind != ND_FUNCALL) return NULL;
  psx_type_t *callee_type = psx_node_get_type(fn->callee);
  if (!callee_type || callee_type->kind != PSX_TYPE_POINTER) return NULL;

  if (callee_type->returns_void) return type_new_void();
  if (callee_type->returns_complex) {
    int complex_size =
        callee_type->pointee_fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, callee_type->pointee_fp_kind,
                                  complex_size, 0, 1, 0);
  }

  psx_ret_pointee_array_t ret_array = psx_ret_pointee_array_make(
      callee_type->funcptr_ret_pointee_array_first_dim,
      callee_type->funcptr_ret_pointee_array_second_dim,
      callee_type->funcptr_ret_pointee_array_elem_size);
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int ignored_ptr = 0;
  tag_type_from_type(callee_type, &tag_kind, &tag_name, &tag_len, &ignored_ptr);
  if (!callee_type->returns_data_pointer &&
      (tag_kind == TK_STRUCT || tag_kind == TK_UNION)) {
    int size = callee_type->base ? psx_type_sizeof(callee_type->base) : 0;
    if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_type_new_tag(tag_kind, tag_name, tag_len,
                            callee_type->base ? callee_type->base->tag_scope_depth_p1 : 0,
                            size);
  }

  if (callee_type->returns_data_pointer || psx_ret_pointee_array_has_dims(ret_array)) {
    psx_type_t *base = NULL;
    if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
      int size = callee_type->base ? psx_type_sizeof(callee_type->base) : 0;
      if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      base = psx_type_new_tag(tag_kind, tag_name, tag_len,
                              callee_type->base ? callee_type->base->tag_scope_depth_p1 : 0,
                              size);
    } else if (callee_type->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float(callee_type->pointee_fp_kind,
                                callee_type->pointee_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    } else {
      int base_size = callee_type->base_deref_size > 0
                          ? callee_type->base_deref_size
                          : callee_type->deref_size;
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
    PSX_RET_POINTEE_ARRAY_STORE_SHORT_FIELDS_IF_PRESENT(type, ret_array);
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
      type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
    }
    type->base_deref_size = psx_type_sizeof(base);
    type->pointer_qual_levels = 1;
    return type;
  }

  if (callee_type->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    return psx_type_new_float(callee_type->pointee_fp_kind,
                              callee_type->pointee_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  int width = callee_type->funcptr_ret_int_width;
  if (width <= 0) width = 4;
  return psx_type_new_integer(TK_EOF, width,
                              callee_type->base && callee_type->base->is_unsigned);
}

static psx_type_t *type_from_indirect_funcall(node_func_t *fn) {
  psx_type_t *from_callee_type = type_from_funcptr_callee_type(fn);
  if (from_callee_type) return from_callee_type;

  node_mem_t *cm = funcall_callee_mem(fn);
  if (!cm) return NULL;
  if (cm->funcptr_ret_is_void) return type_new_void();
  if (cm->funcptr_ret_is_complex) {
    int complex_size = fn->base.fp_kind == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    return type_from_scalar_shape(TK_EOF, (tk_float_kind_t)fn->base.fp_kind,
                                  complex_size, 0, 1, 0);
  }

  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  psx_node_get_tag_type(fn->callee, &tag_kind, &tag_name, &tag_len, NULL);
  if (!cm->funcptr_ret_is_data_pointer &&
      (tag_kind == TK_STRUCT || tag_kind == TK_UNION)) {
    int size = fn->base.ret_struct_size;
    if (size <= 0) size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_type_new_tag(tag_kind, tag_name, tag_len,
                            psx_node_get_tag_scope_depth(fn->callee) + 1, size);
  }

  psx_ret_pointee_array_t ret_array = PSX_RET_POINTEE_ARRAY_FROM_FIELDS(cm);
  int returns_data_pointer =
      cm->funcptr_ret_is_data_pointer || psx_ret_pointee_array_has_dims(ret_array);
  if (returns_data_pointer) {
    tk_float_kind_t ret_pointee_fp = (tk_float_kind_t)fn->base.fp_kind;
    if (ret_pointee_fp == TK_FLOAT_KIND_NONE && psx_ret_pointee_array_has_dims(ret_array))
      ret_pointee_fp = (tk_float_kind_t)cm->pointee_fp_kind;
    psx_type_t *base = NULL;
    if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
      int size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      base = psx_type_new_tag(tag_kind, tag_name, tag_len,
                              psx_node_get_tag_scope_depth(fn->callee) + 1, size);
    } else if (ret_pointee_fp != TK_FLOAT_KIND_NONE) {
      base = psx_type_new_float(ret_pointee_fp,
                                ret_pointee_fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    } else {
      int base_size = cm->base_deref_size > 0 ? cm->base_deref_size : cm->deref_size;
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
    PSX_RET_POINTEE_ARRAY_STORE_SHORT_FIELDS_IF_PRESENT(type, ret_array);
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      type->outer_stride = psx_ret_pointee_array_inner_stride(ret_array);
      type->mid_stride = psx_ret_pointee_array_next_stride(ret_array);
    }
    type->base_deref_size = psx_type_sizeof(base);
    type->pointer_qual_levels = psx_node_pointer_qual_levels(fn->callee) >= 2 ? 1 : 0;
    return type;
  }

  int width = cm->funcptr_ret_int_width;
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
  if (node->type && (!is_mem_node_kind(node->kind) || node_explicit_cast_type(node)))
    return node->type;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return type_from_mem(as_mem(node));
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
  psx_type_t *explicit_type = node_explicit_cast_type(node);
  if (explicit_type) {
    int s = psx_type_sizeof(explicit_type);
    if (s > 0) return s;
  }
  if (node->kind == ND_FUNCALL) {
    int s = psx_type_sizeof(psx_node_get_type(node));
    if (s > 0) return s;
  }
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.type_size;
    case ND_GVAR: return as_mem(node)->type_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return as_mem(node)->type_size;
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
  switch (node->kind) {
    case ND_LVAR: {
      int s = psx_type_deref_size(psx_node_get_type(node));
      return s > 0 ? s : as_lvar(node)->mem.deref_size;
    }
    case ND_GVAR: return as_mem(node)->deref_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
    {
      int s = psx_type_deref_size(psx_node_get_type(node));
      return s > 0 ? s : as_mem(node)->deref_size;
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
      psx_type_t *type = psx_node_get_type(node);
      return psx_type_deref_size(type);
    }
    default:
      return 0;
  }
}

int ps_node_is_pointer(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return psx_type_is_pointer(psx_node_get_type(node)) || as_lvar(node)->mem.is_pointer;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return psx_type_is_pointer(psx_node_get_type(node)) || as_mem(node)->is_pointer;
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
      return psx_type_is_pointer(psx_node_get_type(node));
    case ND_FUNCREF:
      /* 関数シンボルは関数ポインタ値。 */
      return 1;
    default:
      return 0;
  }
}

int psx_node_pointer_qual_levels(node_t *node) {
  if (!node) return 0;
  psx_type_t *explicit_type = node_explicit_cast_type(node);
  if (explicit_type && explicit_type->pointer_qual_levels > 0)
    return explicit_type->pointer_qual_levels;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.pointer_qual_levels;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return as_mem(node)->pointer_qual_levels;
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
      psx_type_t *type = psx_node_get_type(node);
      if (type && type->pointer_qual_levels >= 2) return type->pointer_qual_levels;
      return 0;
    }
    default:
      return 0;
  }
}

int psx_node_base_deref_size(node_t *node) {
  if (!node) return 0;
  psx_type_t *explicit_type = node_explicit_cast_type(node);
  if (explicit_type && explicit_type->base_deref_size > 0)
    return explicit_type->base_deref_size;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.base_deref_size;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return as_mem(node)->base_deref_size;
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
      psx_type_t *type = psx_node_get_type(node);
      if (type && type->pointer_qual_levels >= 2 && type->base_deref_size > 0)
        return type->base_deref_size;
      return 0;
    }
    default:
      return 0;
  }
}

int psx_node_ptr_array_pointee_bytes(node_t *node) {
  if (!node) return 0;
  psx_type_t *explicit_type = node_explicit_cast_type(node);
  if (explicit_type && explicit_type->ptr_array_pointee_bytes > 0)
    return explicit_type->ptr_array_pointee_bytes;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.ptr_array_pointee_bytes;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return as_mem(node)->ptr_array_pointee_bytes;
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
      psx_type_t *type = psx_node_get_type(node);
      if (type && type->ptr_array_pointee_bytes > 0)
        return type->ptr_array_pointee_bytes;
      return 0;
    }
    default:
      return 0;
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

unsigned short psx_node_funcptr_param_fp_mask(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->funcptr_param_fp_mask) return type->funcptr_param_fp_mask;
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->funcptr_param_fp_mask : 0;
}

unsigned short psx_node_funcptr_param_int_mask(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->funcptr_param_int_mask) return type->funcptr_param_int_mask;
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->funcptr_param_int_mask : 0;
}

int psx_node_funcptr_returns_void(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->returns_void) return 1;
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->funcptr_ret_is_void : 0;
}

int psx_node_funcptr_returns_complex(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->returns_complex) return 1;
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->funcptr_ret_is_complex : 0;
}

int psx_node_funcptr_returns_pointee_array(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->funcptr_ret_pointee_array_first_dim > 0) return 1;
  node_mem_t *mem = node_mem_view(node);
  return mem ? PSX_RET_POINTEE_ARRAY_FIELDS_PRESENT(mem) : 0;
}

void psx_node_copy_funcptr_metadata(node_mem_t *dst, node_t *src) {
  if (!dst || !src) return;
  psx_type_t *type = psx_node_get_type(src);
  if (type) {
    if (type->funcptr_param_fp_mask) dst->funcptr_param_fp_mask = type->funcptr_param_fp_mask;
    if (type->funcptr_param_int_mask) dst->funcptr_param_int_mask = type->funcptr_param_int_mask;
    if (type->funcptr_ret_int_width) dst->funcptr_ret_int_width = type->funcptr_ret_int_width;
    if (type->funcptr_nargs_fixed) dst->funcptr_nargs_fixed = type->funcptr_nargs_fixed;
    if (type->returns_void) dst->funcptr_ret_is_void = 1;
    if (type->returns_data_pointer) dst->funcptr_ret_is_data_pointer = 1;
    if (type->returns_complex) dst->funcptr_ret_is_complex = 1;
    PSX_RET_POINTEE_ARRAY_STORE_SHORT_FIELDS_IF_PRESENT(dst, psx_ret_pointee_array_make(
        type->funcptr_ret_pointee_array_first_dim,
        type->funcptr_ret_pointee_array_second_dim,
        type->funcptr_ret_pointee_array_elem_size));
  }

  node_mem_t *mem = node_mem_view(src);
  if (!mem) return;
  if (!dst->funcptr_param_fp_mask && mem->funcptr_param_fp_mask)
    dst->funcptr_param_fp_mask = mem->funcptr_param_fp_mask;
  if (!dst->funcptr_param_int_mask && mem->funcptr_param_int_mask)
    dst->funcptr_param_int_mask = mem->funcptr_param_int_mask;
  if (!dst->funcptr_ret_int_width && mem->funcptr_ret_int_width)
    dst->funcptr_ret_int_width = mem->funcptr_ret_int_width;
  if (!dst->funcptr_nargs_fixed && mem->funcptr_nargs_fixed)
    dst->funcptr_nargs_fixed = mem->funcptr_nargs_fixed;
  if (mem->funcptr_ret_is_void) dst->funcptr_ret_is_void = 1;
  if (mem->funcptr_ret_is_data_pointer) dst->funcptr_ret_is_data_pointer = 1;
  if (mem->funcptr_ret_is_complex) dst->funcptr_ret_is_complex = 1;
  if (!PSX_RET_POINTEE_ARRAY_FIELDS_PRESENT(dst) &&
      PSX_RET_POINTEE_ARRAY_FIELDS_PRESENT(mem)) {
    PSX_RET_POINTEE_ARRAY_COPY_FIELDS(dst, mem);
  }
}

void psx_node_copy_funcptr_metadata_from_lvar(node_mem_t *dst, const lvar_t *src) {
  if (!dst || !src) return;
  dst->funcptr_param_fp_mask = src->funcptr_param_fp_mask;
  dst->funcptr_param_int_mask = src->funcptr_param_int_mask;
  dst->funcptr_ret_int_width = src->funcptr_ret_int_width;
  dst->is_variadic_funcptr = src->is_variadic_funcptr ? 1 : 0;
  dst->funcptr_nargs_fixed = src->funcptr_nargs_fixed;
  dst->funcptr_ret_is_void = src->funcptr_ret_is_void ? 1 : 0;
  dst->funcptr_ret_is_data_pointer = src->funcptr_ret_is_data_pointer ? 1 : 0;
  dst->funcptr_ret_is_complex = src->funcptr_ret_is_complex ? 1 : 0;
  PSX_RET_POINTEE_ARRAY_COPY_FIELDS(dst, src);
}

void psx_node_copy_funcptr_metadata_from_gvar(node_mem_t *dst, const global_var_t *src) {
  if (!dst || !src) return;
  dst->funcptr_param_fp_mask = src->funcptr_param_fp_mask;
  dst->funcptr_param_int_mask = src->funcptr_param_int_mask;
  dst->funcptr_ret_int_width = src->funcptr_ret_int_width;
  dst->is_variadic_funcptr = src->is_variadic_funcptr ? 1 : 0;
  dst->funcptr_nargs_fixed = src->funcptr_nargs_fixed;
  dst->funcptr_ret_is_void = src->funcptr_ret_is_void ? 1 : 0;
  dst->funcptr_ret_is_data_pointer = src->funcptr_ret_is_data_pointer ? 1 : 0;
  dst->funcptr_ret_is_complex = src->funcptr_ret_is_complex ? 1 : 0;
  PSX_RET_POINTEE_ARRAY_COPY_FIELDS(dst, src);
}

void psx_node_copy_funcptr_metadata_from_tag_member(node_mem_t *dst,
                                                    const tag_member_info_t *src) {
  if (!dst || !src) return;
  dst->funcptr_param_fp_mask = src->funcptr_param_fp_mask;
  dst->funcptr_param_int_mask = src->funcptr_param_int_mask;
  dst->funcptr_ret_int_width = src->funcptr_ret_int_width;
  dst->is_variadic_funcptr = src->is_variadic_funcptr ? 1 : 0;
  dst->funcptr_nargs_fixed = src->funcptr_nargs_fixed;
  dst->funcptr_ret_is_void = src->funcptr_ret_is_void ? 1 : 0;
  dst->funcptr_ret_is_data_pointer = src->funcptr_ret_is_pointer ? 1 : 0;
  dst->funcptr_ret_is_complex = src->funcptr_ret_is_complex ? 1 : 0;
  PSX_RET_POINTEE_ARRAY_STORE_SHORT_FIELDS_IF_PRESENT(
      dst, src->funcptr_ret_pointee_array);
}

void psx_node_init_lvar_array_addr_metadata(node_mem_t *addr, const lvar_t *var,
                                            int is_tag_pointer) {
  if (!addr || !var) return;
  int stride = (var->outer_stride > 0) ? var->outer_stride : var->elem_size;
  addr->type_size = (short)stride;
  addr->deref_size = (short)stride;
  addr->is_pointer = 1;
  addr->pointee_fp_kind = var->pointee_fp_kind != TK_FLOAT_KIND_NONE
                             ? (unsigned int)var->pointee_fp_kind
                             : (unsigned int)var->fp_kind;
  addr->pointee_is_bool = var->is_bool ? 1 : 0;
  addr->pointee_is_unsigned = var->is_unsigned ? 1 : 0;
  psx_node_copy_funcptr_metadata_from_lvar(addr, var);
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
  addr->tag_kind = var->tag_kind;
  addr->tag_name = var->tag_name;
  addr->tag_len = var->tag_len;
  addr->tag_scope_depth_p1 = var->tag_scope_depth_p1;
  addr->is_tag_pointer = is_tag_pointer ? 1 : 0;
  addr->is_const_qualified = var->is_const_qualified ? 1 : 0;
  addr->is_volatile_qualified = var->is_volatile_qualified ? 1 : 0;
  addr->pointer_qual_levels = var->pointer_qual_levels;
  addr->base_deref_size = var->base_deref_size;
  addr->ptr_array_pointee_bytes = var->ptr_array_pointee_bytes;
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
  int stride = (gv->outer_stride > 0) ? gv->outer_stride : gv->deref_size;
  addr->type_size = (short)stride;
  addr->deref_size = (short)stride;
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

unsigned int psx_node_pointer_const_qual_mask(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->pointer_const_qual_mask) return type->pointer_const_qual_mask;
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
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->pointer_const_qual_mask : 0;
    }
  }
}

unsigned int psx_node_pointer_volatile_qual_mask(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->pointer_volatile_qual_mask) return type->pointer_volatile_qual_mask;
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
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->pointer_volatile_qual_mask : 0;
    }
  }
}

int psx_node_pointee_is_unsigned(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) &&
      type->base) {
    return psx_type_is_unsigned(type->base);
  }
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
    default: {
      node_mem_t *mem = node_mem_view(node);
      return mem ? (mem->pointee_is_unsigned || mem->is_unsigned) : 0;
    }
  }
}

int psx_node_pointee_is_bool(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) &&
      type->base) {
    return type->base->kind == PSX_TYPE_BOOL;
  }
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
    default: {
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->pointee_is_bool : 0;
    }
  }
}

int psx_node_pointee_is_void(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) &&
      type->base) {
    return type->base->kind == PSX_TYPE_VOID;
  }
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
    default: {
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->pointee_is_void : 0;
    }
  }
}

int psx_node_pointee_is_const_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) &&
      type->base) {
    return type->base->is_const_qualified ? 1 : 0;
  }
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
    default: {
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->is_const_qualified : 0;
    }
  }
}

int psx_node_pointee_is_volatile_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) &&
      type->base) {
    return type->base->is_volatile_qualified ? 1 : 0;
  }
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
    default: {
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->is_volatile_qualified : 0;
    }
  }
}

int psx_node_is_unsigned_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)
    return psx_type_is_unsigned(type);
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
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->is_unsigned : (node->is_unsigned ? 1 : 0);
    }
  }
}

int psx_node_is_long_long_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->is_long_long) return 1;
  switch (node->kind) {
    case ND_NUM:
      return ((node_num_t *)node)->int_is_long_long ? 1 : 0;
    case ND_COMMA:
    case ND_STMT_EXPR:
      return psx_node_is_long_long_type(node->rhs);
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      return psx_node_is_long_long_type(ctrl->base.rhs) ||
             psx_node_is_long_long_type(ctrl->els);
    }
    default: {
      node_mem_t *mem = node_mem_view(node);
      return mem ? mem->is_long_long : (node->is_long_long ? 1 : 0);
    }
  }
}

int psx_node_is_plain_char_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->is_plain_char) return 1;
  if (node->kind == ND_NUM) return ((node_num_t *)node)->int_is_plain_char ? 1 : 0;
  if (node->kind == ND_COMMA || node->kind == ND_STMT_EXPR)
    return psx_node_is_plain_char_type(node->rhs);
  if (node->kind == ND_TERNARY) {
    node_ctrl_t *ctrl = (node_ctrl_t *)node;
    return psx_node_is_plain_char_type(ctrl->base.rhs) ||
           psx_node_is_plain_char_type(ctrl->els);
  }
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->is_plain_char : 0;
}

int psx_node_is_long_double_type(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = psx_node_get_type(node);
  if (type && type->is_long_double) return 1;
  if (node->kind == ND_COMMA || node->kind == ND_STMT_EXPR)
    return psx_node_is_long_double_type(node->rhs);
  if (node->kind == ND_TERNARY) {
    node_ctrl_t *ctrl = (node_ctrl_t *)node;
    return psx_node_is_long_double_type(ctrl->base.rhs) ||
           psx_node_is_long_double_type(ctrl->els);
  }
  node_mem_t *mem = node_mem_view(node);
  return mem ? mem->is_long_double : 0;
}

tk_float_kind_t psx_node_pointee_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  psx_type_t *explicit_type = node_explicit_cast_type(node);
  if (explicit_type && explicit_type->pointee_fp_kind != TK_FLOAT_KIND_NONE)
    return explicit_type->pointee_fp_kind;
  switch (node->kind) {
    case ND_LVAR: return (tk_float_kind_t)as_lvar(node)->mem.pointee_fp_kind;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
      return (tk_float_kind_t)as_mem(node)->pointee_fp_kind;
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
      psx_type_t *type = psx_node_get_type(node);
      if (type && type->pointee_fp_kind != TK_FLOAT_KIND_NONE)
        return type->pointee_fp_kind;
      return TK_FLOAT_KIND_NONE;
    }
    default:
      return TK_FLOAT_KIND_NONE;
  }
}

/* pointer-to-VLA (`int (*p)[m]`) の行ストライドスロット (実行時値) のフレームオフセット。
 * 無ければ 0。ポインタ算術 (`p + 1`) のスケールに使う。ND_ADD/SUB は被演算子を辿る。 */
int psx_node_vla_row_stride_frame_off(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.vla_row_stride_frame_off;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
      return as_mem(node)->vla_row_stride_frame_off;
    case ND_ADD:
    case ND_SUB: {
      int l = psx_node_vla_row_stride_frame_off(node->lhs);
      if (l != 0) return l;
      return psx_node_vla_row_stride_frame_off(node->rhs);
    }
    default:
      return 0;
  }
}

void psx_node_get_tag_type(node_t *node, token_kind_t *tag_kind, char **tag_name, int *tag_len, int *is_tag_pointer) {
  token_kind_t kind = TK_EOF;
  char *name = NULL;
  int len = 0;
  int ptr = 0;
  if (node) {
    if (node->kind == ND_FUNCALL &&
        tag_type_from_type(psx_node_get_type(node), &kind, &name, &len, &ptr)) {
      goto out;
    }
    switch (node->kind) {
      case ND_LVAR:
        kind = as_lvar(node)->mem.tag_kind;
        name = as_lvar(node)->mem.tag_name;
        len = as_lvar(node)->mem.tag_len;
        ptr = as_lvar(node)->mem.is_tag_pointer;
        break;
      case ND_GVAR:
      case ND_DEREF:
      case ND_ADDR:
      case ND_STRING:
      case ND_CAST:
        kind = as_mem(node)->tag_kind;
        name = as_mem(node)->tag_name;
        len = as_mem(node)->tag_len;
        ptr = as_mem(node)->is_tag_pointer;
        break;
      case ND_ASSIGN:
        /* 代入式の結果は左辺の型。ノード自身に tag が無い (複合代入 `p += n` 等)
         * 場合は左辺から継承して `(p += n)->m` を解決できるようにする。 */
        kind = as_mem(node)->tag_kind;
        name = as_mem(node)->tag_name;
        len = as_mem(node)->tag_len;
        ptr = as_mem(node)->is_tag_pointer;
        if (kind == TK_EOF) {
          psx_node_get_tag_type(node->lhs, &kind, &name, &len, &ptr);
        }
        break;
      case ND_COMMA:
        psx_node_get_tag_type(node->rhs, &kind, &name, &len, &ptr);
        break;
      /* `p + n` のようなポインタ算術: tag info を pointer 側 (lhs) から継承する。
       * `(p+1)->x` や `(p+i).x` (`.` は通常 lvalue のみだが parser が許す形) で
       * tag が引けないと arrow/dot がエラーになる。 */
      case ND_ADD:
      case ND_SUB:
        psx_node_get_tag_type(node->lhs, &kind, &name, &len, &ptr);
        if (kind == TK_EOF) {
          psx_node_get_tag_type(node->rhs, &kind, &name, &len, &ptr);
        }
        break;
      /* `(cond ? a : b).x` 等の struct ternary 結果からメンバアクセスする際、
       * 両分岐は同型 struct のはずなので then 側から tag を引く。 */
      case ND_TERNARY: {
        node_ctrl_t *t = (node_ctrl_t *)node;
        psx_node_get_tag_type(t->base.rhs, &kind, &name, &len, &ptr);
        if (kind == TK_EOF && t->els) {
          psx_node_get_tag_type(t->els, &kind, &name, &len, &ptr);
        }
        break;
      }
      case ND_FUNCALL:
        break;
      /* `(++p)->m` / `(p++)->m`: inc/dec はオペランドと同じ型なので tag を継承する。 */
      case ND_PRE_INC:
      case ND_PRE_DEC:
      case ND_POST_INC:
      case ND_POST_DEC:
        psx_node_get_tag_type(node->lhs, &kind, &name, &len, &ptr);
        break;
      default:
        break;
    }
  }
out:
  if (tag_kind) *tag_kind = kind;
  if (tag_name) *tag_name = name;
  if (tag_len) *tag_len = len;
  if (is_tag_pointer) *is_tag_pointer = ptr;
}

int psx_node_get_tag_scope_depth(node_t *node) {
  if (!node) return -1;
  int p1 = 0;
  switch (node->kind) {
    case ND_LVAR:
      p1 = as_lvar(node)->mem.tag_scope_depth_p1;
      break;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST:
    case ND_ASSIGN:
      p1 = as_mem(node)->tag_scope_depth_p1;
      break;
    case ND_COMMA:
      return psx_node_get_tag_scope_depth(node->rhs);
    case ND_ADD:
    case ND_SUB:
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
  return p1 > 0 ? p1 - 1 : -1;
}

static int node_is_unsigned(node_t *node) {
  if (!node) return 0;
  psx_type_t *explicit_type = node_explicit_cast_type(node);
  if (explicit_type && explicit_type->kind != PSX_TYPE_POINTER)
    return psx_type_is_unsigned(explicit_type);
  switch (node->kind) {
    case ND_LVAR:
      return psx_type_is_unsigned(psx_node_get_type(node)) || as_lvar(node)->mem.is_unsigned;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
      return psx_type_is_unsigned(psx_node_get_type(node)) || as_mem(node)->is_unsigned;
    case ND_CAST:
      return psx_type_is_unsigned(psx_node_get_type(node)) || as_mem(node)->is_unsigned || node->is_unsigned;
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
    case ND_SHL:
    case ND_SHR:
      /* Shift signedness doubles as the codegen ASR/LSR selector. Cast lowering
       * may override it independently of the lhs/result type. */
      return node->is_unsigned;
    default: return node->is_unsigned;
  }
}

static int node_is_long_long(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_NUM:
      return ((node_num_t *)node)->int_is_long_long ? 1 : 0;
    case ND_LVAR:
      return (psx_node_get_type(node) && psx_node_get_type(node)->is_long_long) ||
             as_lvar(node)->mem.is_long_long ? 1 : 0;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_CAST:
      return (psx_node_get_type(node) && psx_node_get_type(node)->is_long_long) ||
             as_mem(node)->is_long_long ? 1 : 0;
    case ND_TERNARY: {
      node_ctrl_t *t = (node_ctrl_t *)node;
      return node_is_long_long(t->base.rhs) || node_is_long_long(t->els);
    }
    default:
      return node->is_long_long ? 1 : 0;
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
    if (lhs_sz >= 8 && node_is_long_long(lhs)) node->is_long_long = 1;
  } else if (kind == ND_ADD || kind == ND_SUB || kind == ND_MUL ||
             kind == ND_DIV || kind == ND_MOD || kind == ND_BITAND ||
             kind == ND_BITXOR || kind == ND_BITOR) {
    node->is_unsigned = binary_usual_arith_unsigned(lhs, rhs) ? 1 : 0;
    if (ps_node_type_size(node) >= 8 &&
        (node_is_long_long(lhs) || node_is_long_long(rhs))) {
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

lvar_t *psx_node_lvar_symbol(node_t *node) {
  if (!node || node->kind != ND_LVAR) return NULL;
  node_lvar_t *lv = (node_lvar_t *)node;
  return lv->var ? lv->var : psx_decl_find_lvar_by_offset(lv->offset);
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
  node->base.rhs = rhs;
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
    node_mem_t *mem = as_mem(node);
    /* ag_c の慣習: ポインタ変数の is_const_qualified は「pointee の const」を
     * 表す (_Generic の判定等で利用)。「変数自身の const」は
     * pointer_const_qual_mask の bit 0 で保持される。
     * したがって p = q を拒否するのはこのビットが立っているときのみ
     * (`int * const p;` のケース)。非ポインタ変数は従来通り
     * is_const_qualified を見る (`const int x = 5; x = 10;` を拒否)。 */
    int self_const = mem->is_pointer
                         ? (int)(mem->pointer_const_qual_mask & 1u)
                         : mem->is_const_qualified;
    if (self_const) {
      diag_emit_tokf(DIAG_ERR_PARSER_CONST_ASSIGNMENT, curtok(),
                     diag_message_for(DIAG_ERR_PARSER_CONST_ASSIGNMENT));
    }
  }
}

static int node_pointee_is_const(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
    case ND_CAST: {
      node_mem_t *m = (node_mem_t *)node;
      return m->is_tag_pointer && m->is_const_qualified;
    }
    case ND_COMMA:
      return node_pointee_is_const(node->rhs);
    default:
      return 0;
  }
}

void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs) return;
  if (lhs->kind != ND_LVAR && lhs->kind != ND_GVAR) return;
  node_mem_t *lhs_mem = as_mem(lhs);
  if (!lhs_mem->is_tag_pointer) return;
  if (lhs_mem->is_const_qualified) return;
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
  /* C11 6.3.1.2: _Bool への (複合) 代入は結果を 0/1 に正規化する。
   * 通常代入と同様、op の結果を (result != 0) で包む。 */
  int lhs_is_bool = 0;
  if (lhs && (lhs->kind == ND_LVAR || lhs->kind == ND_DEREF || lhs->kind == ND_GVAR)) {
    lhs_is_bool = ((node_mem_t *)lhs)->is_bool;
  }
  if (lhs_is_bool) {
    op_expr = psx_node_new_binary(ND_NE, op_expr, psx_node_new_num(0));
  }
  node_mem_t *assign_node = psx_node_new_assign(lhs, op_expr);
  assign_node->type_size = ps_node_type_size(lhs);
  assign_node->base.fp_kind = lhs ? lhs->fp_kind : 0;
  return (node_t *)assign_node;
}
