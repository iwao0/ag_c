#include "node_utils.h"
#include "lvar_internal.h"
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

_Static_assert(sizeof(node_num_t) >= sizeof(node_source_cast_t),
               "source cast arena slot must hold lowered numeric nodes");

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
static int node_pointer_stride_from_type(
    const psx_type_t *type, int *inner_stride, int *next_stride,
    int *extra_strides, int *extra_strides_count);
static int node_self_is_const_qualified(node_t *node);
static int node_self_is_volatile_qualified(node_t *node);
static psx_type_t *type_decay_array_to_pointer(psx_type_t *array_type);
static const psx_type_t *type_pointee_value_type(const psx_type_t *type);
static void gvar_tag_identity(const global_var_t *gv, token_kind_t *kind,
                              char **name, int *len, int *scope_depth_p1);
static int node_scalar_ptr_member_lvalue(node_t *node);
static psx_type_t *type_from_deref_operand(node_t *operand);

static int is_lvalue_clone_kind(node_kind_t kind) {
  return kind == ND_LVAR || kind == ND_GVAR || kind == ND_UNARY_DEREF ||
         kind == ND_SUBSCRIPT ||
         kind == ND_DEREF ||
         kind == ND_STRING;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_type(const psx_type_t *type) {
  return ps_type_funcptr_signature(type);
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

psx_decl_funcptr_sig_t ps_node_funcdef_ret_funcptr_sig(const node_func_t *fn) {
  if (!fn || !fn->function_type ||
      fn->function_type->kind != PSX_TYPE_FUNCTION ||
      !fn->function_type->base)
    return (psx_decl_funcptr_sig_t){0};
  return ps_type_funcptr_signature(fn->function_type->base);
}

static int tag_scope_depth_from_p1(int scope_depth_p1) {
  return scope_depth_p1 > 0 ? scope_depth_p1 - 1 : -1;
}

static int ctx_get_tag_member_count_scoped(token_kind_t tk, char *tn, int tl,
                                           int scope_depth_p1) {
  int scope_depth = tag_scope_depth_from_p1(scope_depth_p1);
  if (scope_depth >= 0) {
    int n = ps_ctx_get_tag_member_count_at_scope(tk, tn, tl, scope_depth);
    if (n >= 0) return n;
  }
  return ps_ctx_get_tag_member_count(tk, tn, tl);
}

static int ctx_get_tag_member_info_scoped(token_kind_t tk, char *tn, int tl,
                                          int scope_depth_p1, int idx,
                                          tag_member_info_t *out) {
  int scope_depth = tag_scope_depth_from_p1(scope_depth_p1);
  if (scope_depth >= 0) {
    return ps_ctx_get_tag_member_info_at_scope(tk, tn, tl, scope_depth, idx, out);
  }
  return ps_ctx_get_tag_member_info(tk, tn, tl, idx, out);
}

static int type_pointer_depth(const psx_type_t *type) {
  int depth = 0;
  while (type && type->kind == PSX_TYPE_POINTER) {
    depth++;
    type = type->base;
  }
  return depth;
}

static psx_type_t *type_with_funcptr_sig(psx_type_t *type,
                                         psx_decl_funcptr_sig_t sig) {
  if (!type || !ps_decl_funcptr_sig_has_payload(sig) ||
      ps_decl_funcptr_sig_has_payload(type->funcptr_sig)) {
    return type;
  }
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
  return copy;
}

static int funcptr_sig_equal(psx_decl_funcptr_sig_t a, psx_decl_funcptr_sig_t b) {
  return psx_funcptr_type_shape_matches(a.function, b.function);
}

static psx_type_t *type_with_funcptr_sig_merged(psx_type_t *type,
                                                psx_decl_funcptr_sig_t sig) {
  if (!type || !ps_decl_funcptr_sig_has_payload(sig)) return type;
  psx_decl_funcptr_sig_t merged =
      funcptr_sig_merge_missing(type->funcptr_sig, &sig, 1);
  if (funcptr_sig_equal(type->funcptr_sig, merged)) return type;
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->funcptr_sig = ps_decl_funcptr_sig_clone(merged);
  return copy;
}

static psx_type_t *type_with_self_qualifiers(psx_type_t *type,
                                             int is_const_qualified,
                                             int is_volatile_qualified) {
  if (!type) return NULL;
  psx_type_t *copy = arena_alloc(sizeof(psx_type_t));
  *copy = *type;
  copy->funcptr_sig = ps_decl_funcptr_sig_clone(type->funcptr_sig);
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
  return var ? var->decl_type : NULL;
}

static psx_type_t *gvar_decl_type_consistent(global_var_t *gv) {
  return gv ? gv->decl_type : NULL;
}

int ps_lvar_value_is_pointer_like(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  return type ? ps_type_is_pointer(type) : 0;
}

int ps_lvar_is_struct_aggregate(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  return type ? type_tag_aggregate_kind(type) == TK_STRUCT : 0;
}

int ps_lvar_is_union_aggregate(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  return type ? type_tag_aggregate_kind(type) == TK_UNION : 0;
}

int ps_lvar_is_tag_aggregate(const lvar_t *var) {
  return ps_lvar_is_struct_aggregate(var) || ps_lvar_is_union_aggregate(var);
}

static const psx_type_t *type_skip_array_views_const(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static int lvar_self_is_const_qualified(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  const psx_type_t *value_type = type_skip_array_views_const(type);
  if (type_is_pointer_view_type(value_type))
    return (ps_type_pointer_view_structural_qual_mask(value_type, 0) & 1u)
               ? 1
               : 0;
  return value_type && value_type->is_const_qualified ? 1 : 0;
}

static int lvar_self_is_volatile_qualified(const lvar_t *var) {
  psx_type_t *type = lvar_decl_type_view(var);
  const psx_type_t *value_type = type_skip_array_views_const(type);
  if (type_is_pointer_view_type(value_type))
    return (ps_type_pointer_view_structural_qual_mask(value_type, 1) & 1u)
               ? 1
               : 0;
  return value_type && value_type->is_volatile_qualified ? 1 : 0;
}

psx_type_t *ps_lvar_get_decl_type(lvar_t *var) {
  return lvar_decl_type_consistent(var);
}

int ps_gvar_storage_size(const global_var_t *gv, int fallback_size) {
  int decl_size = ps_gvar_decl_sizeof(gv, 0);
  int storage_size = gv && gv->type_size > 0 ? gv->type_size : 0;
  if (storage_size > decl_size) return storage_size;
  if (decl_size > 0) return decl_size;
  return storage_size > 0 ? storage_size : fallback_size;
}

int ps_gvar_decl_sizeof(const global_var_t *gv, int fallback_size) {
  psx_type_t *type = gvar_decl_type_view(gv);
  int size = ps_type_sizeof(type);
  return size > 0 ? size : fallback_size;
}

int ps_gvar_is_array(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  return type && type->kind == PSX_TYPE_ARRAY ? 1 : 0;
}

int ps_gvar_is_struct_aggregate(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  return type ? type_tag_aggregate_kind(type) == TK_STRUCT : 0;
}

int ps_gvar_is_union_aggregate(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  return type ? type_tag_aggregate_kind(type) == TK_UNION : 0;
}

int ps_gvar_is_tag_aggregate(const global_var_t *gv) {
  return ps_gvar_is_struct_aggregate(gv) || ps_gvar_is_union_aggregate(gv);
}

static const psx_type_t *type_skip_arrays_const(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

int ps_gvar_is_bool_scalar(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  return type && type->kind == PSX_TYPE_BOOL ? 1 : 0;
}

int ps_gvar_array_element_is_bool(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  const psx_type_t *leaf = type_skip_arrays_const(type);
  return leaf && leaf->kind == PSX_TYPE_BOOL ? 1 : 0;
}

psx_gvar_initializer_class_t
ps_gvar_initializer_class(const global_var_t *gv, int include_empty_aggregate) {
  psx_gvar_view_t view = ps_gvar_view(gv);
  int is_tag_aggregate = ps_gvar_is_tag_aggregate(gv);
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

int ps_gvar_has_aggregate_initializer(const global_var_t *gv) {
  return ps_gvar_initializer_class(gv, 0).has_aggregate_initializer;
}

int ps_gvar_has_explicit_initializer(const global_var_t *gv) {
  return ps_gvar_initializer_class(gv, 0).has_explicit_initializer;
}

static psx_gvar_init_slots_layout_t gvar_init_slots_layout(const global_var_t *gv,
                                                           int fallback_size) {
  psx_gvar_view_t view = ps_gvar_view(gv);
  psx_gvar_init_slots_layout_t layout = {
      .elem_size = ps_gvar_initializer_element_size(gv, fallback_size),
      .elem_count = ps_gvar_initializer_element_count(gv, fallback_size),
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
  psx_gvar_view_t view = ps_gvar_view(gv);
  return gvar_make_symbol_ref(view.init_symbol, view.init_symbol_len,
                              view.init_symbol_offset);
}

static psx_gvar_symbol_ref_t gvar_init_slot_symbol_ref(const psx_gvar_init_slot_t *slot) {
  if (!slot) return gvar_make_symbol_ref(NULL, 0, 0);
  return gvar_make_symbol_ref(slot->symbol, slot->symbol_len, slot->value);
}

static psx_gvar_init_slot_value_t gvar_init_slot_value(
    const global_var_t *gv, int idx, const psx_gvar_init_slots_layout_t *layout) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
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

int ps_gvar_walk_init_slot_values(const global_var_t *gv,
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

int ps_gvar_fp_bit_pattern(tk_float_kind_t fp_kind, double value,
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

int ps_gvar_symbol_ref_named(psx_gvar_symbol_ref_t ref,
                              char **out_name, int *out_len) {
  if (out_name) *out_name = NULL;
  if (out_len) *out_len = 0;
  if (ref.kind != PSX_GVAR_SYMBOL_REF_NAMED) return 0;
  if (out_name) *out_name = ref.symbol;
  if (out_len) *out_len = ref.symbol_len;
  return 1;
}

int ps_gvar_symbol_ref_named_function(psx_gvar_symbol_ref_t ref,
                                       char **out_name, int *out_len) {
  char *name = NULL;
  int len = 0;
  if (!ps_gvar_symbol_ref_named(ref, &name, &len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (!ps_ctx_has_function_name(name, len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (out_name) *out_name = name;
  if (out_len) *out_len = len;
  return 1;
}

int ps_gvar_init_value_named_function(psx_gvar_init_value_t value,
                                        char **out_name, int *out_len) {
  if (out_name) *out_name = NULL;
  if (out_len) *out_len = 0;
  if (value.kind != PSX_GVAR_INIT_VALUE_SYMBOL) return 0;
  return ps_gvar_symbol_ref_named_function(value.symbol_ref, out_name, out_len);
}

psx_gvar_init_member_value_t
ps_gvar_init_member_value(const global_var_t *gv, int idx,
                           const tag_member_info_t *member) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  tk_float_kind_t member_fp_kind = ps_tag_member_decl_fp_kind(member);
  psx_gvar_init_member_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_init_slot_symbol_ref(&slot),
      .value = slot.value,
      .fvalue = slot.fvalue,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = ps_tag_member_decl_value_size(member),
  };
  if (ps_tag_member_decl_is_bool(member)) value.value = value.value != 0;
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
ps_gvar_init_scalar_value(const global_var_t *gv, int fallback_size) {
  psx_gvar_view_t view = ps_gvar_view(gv);
  psx_gvar_init_scalar_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_initializer_symbol_ref(gv),
      .value = view.has_init ? view.init_val : 0,
      .fvalue = view.has_init ? view.fval : 0.0,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = ps_gvar_storage_size(gv, fallback_size),
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

int ps_gvar_visit_initializer_classified(
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
      ps_gvar_init_scalar_value(gv, fallback_size);
  return ops->scalar ? ops->scalar(user, value, init_class) : 0;
}

int ps_gvar_visit_initializer(const global_var_t *gv, int include_empty_aggregate,
                               int fallback_size,
                               const psx_gvar_initializer_visit_ops_t *ops,
                               void *user) {
  psx_gvar_initializer_class_t init_class =
      ps_gvar_initializer_class(gv, include_empty_aggregate);
  return ps_gvar_visit_initializer_classified(gv, &init_class, fallback_size,
                                               ops, user);
}

int ps_gvar_array_element_size(const global_var_t *gv) {
  if (!ps_gvar_is_array(gv)) return 0;
  psx_type_t *type = gvar_decl_type_view(gv);
  if (!type || type->kind != PSX_TYPE_ARRAY || !type->base) return 0;
  int elem = ps_type_sizeof(type->base);
  if (elem <= 0) elem = ps_type_deref_size(type);
  return elem > 0 ? elem : 0;
}

int ps_gvar_array_element_count(const global_var_t *gv) {
  psx_type_t *type = gvar_decl_type_view(gv);
  if (type && type->kind == PSX_TYPE_ARRAY && type->array_len > 0)
    return type->array_len;
  int elem = ps_gvar_array_element_size(gv);
  int size = ps_type_sizeof(type);
  if (elem <= 0 || size <= 0) return 0;
  return size / elem;
}

static int type_array_leaf_element_size(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  const psx_type_t *cur = type;
  while (cur && cur->kind == PSX_TYPE_ARRAY && cur->base) {
    if (cur->base->kind != PSX_TYPE_ARRAY) {
      int elem = ps_type_sizeof(cur->base);
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
  if (!type || !ps_type_is_tag_aggregate(type)) return 0;
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
  (void)gvar_tag_identity_from_type(gv, &out_kind, &out_name, &out_len,
                                    &out_scope_depth_p1);
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
    int elem_size = ps_type_sizeof(cur->base);
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
  const psx_aggregate_definition_t *definition;
} gvar_aggregate_layout_t;

typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  int ordinal;
  int count;
  const psx_aggregate_definition_t *definition;
  psx_tag_flat_cover_state_t cover_state;
} gvar_aggregate_member_iter_t;

static gvar_aggregate_layout_t gvar_aggregate_layout(const global_var_t *gv);
static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(token_kind_t tag_kind,
                                                               char *tag_name,
                                                               int tag_len,
                                                               int tag_scope_depth_p1,
                                                               const psx_aggregate_definition_t *definition);
static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      tag_member_info_t *out, int *out_ordinal);
static void gvar_aggregate_member_iter_set_next(gvar_aggregate_member_iter_t *iter,
                                                int next_ordinal);
static int gvar_walk_struct_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                        int tag_scope_depth_p1,
                                        const psx_aggregate_definition_t *definition,
                                        global_var_t *gv, gvar_init_cursor_t *cur,
                                        long long base_offset, int struct_size,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user);
static int gvar_walk_union_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       int tag_scope_depth_p1,
                                       const psx_aggregate_definition_t *definition,
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
                                                     const psx_aggregate_definition_t *definition,
                                                     gvar_init_cursor_t *cur,
                                                     int start_idx);
static int gvar_init_cursor_pack_bitfield_unit(token_kind_t tag_kind, char *tag_name,
                                               int tag_len,
                                               const psx_aggregate_definition_t *definition,
                                               int member_index,
                                               gvar_init_cursor_t *cur,
                                               psx_gvar_bitfield_unit_t *out);
static int tag_union_init_member_for_slot_scoped(token_kind_t tag_kind, char *tag_name,
                                                int tag_len, int tag_scope_depth_p1,
                                                const psx_aggregate_definition_t *definition,
                                                const global_var_t *gv, int idx,
                                                tag_member_info_t *out);

static gvar_aggregate_layout_t gvar_aggregate_layout(const global_var_t *gv) {
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int tag_scope_depth_p1 = 0;
  gvar_tag_identity(gv, &tag_kind, &tag_name, &tag_len, &tag_scope_depth_p1);
  int type_size = ps_gvar_decl_sizeof(gv, 0);
  const psx_type_t *aggregate_type = gvar_decl_type_view(gv);
  while (aggregate_type && aggregate_type->kind == PSX_TYPE_ARRAY)
    aggregate_type = aggregate_type->base;
  if (type_size <= 0 && gv) type_size = gv->type_size;
  gvar_aggregate_layout_t layout = {
      .tag_kind = tag_kind,
      .tag_name = tag_name,
      .tag_len = tag_len,
      .tag_scope_depth_p1 = tag_scope_depth_p1,
      .type_size = type_size,
      .elem_size = type_size,
      .elem_count = 1,
      .is_array = ps_gvar_is_array(gv),
      .is_union = ps_gvar_is_union_aggregate(gv),
      .definition = aggregate_type && ps_type_is_tag_aggregate(aggregate_type)
                        ? aggregate_type->aggregate_definition
                        : NULL,
  };
  if (layout.is_array) {
    layout.elem_size = ps_gvar_initializer_element_size(gv, type_size);
    layout.elem_count = ps_gvar_initializer_element_count(gv, type_size);
  }
  return layout;
}

static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(token_kind_t tag_kind,
                                                               char *tag_name,
                                                               int tag_len,
                                                               int tag_scope_depth_p1,
                                                               const psx_aggregate_definition_t *definition) {
  gvar_aggregate_member_iter_t iter = {
      .tag_kind = tag_kind,
      .tag_name = tag_name,
      .tag_len = tag_len,
      .tag_scope_depth_p1 = tag_scope_depth_p1,
      .ordinal = 0,
      .count = definition
                   ? definition->member_count
                   : ctx_get_tag_member_count_scoped(
                         tag_kind, tag_name, tag_len, tag_scope_depth_p1),
      .definition = definition,
  };
  ps_tag_flat_cover_state_init(&iter.cover_state);
  return iter;
}

static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      tag_member_info_t *out, int *out_ordinal) {
  if (!iter || !out) return 0;
  while (iter->ordinal < iter->count) {
    int ordinal = iter->ordinal++;
    tag_member_info_t mi = {0};
    if (iter->definition) {
      mi = iter->definition->members[ordinal];
    } else if (!ctx_get_tag_member_info_scoped(
                   iter->tag_kind, iter->tag_name, iter->tag_len,
                   iter->tag_scope_depth_p1, ordinal, &mi)) {
        return 0;
      }
    if (ps_tag_member_is_unnamed_struct(&mi)) continue;
    if (ps_tag_flat_cover_state_covers(&iter->cover_state, &mi)) continue;
    ps_tag_flat_cover_state_note(&iter->cover_state, iter->tag_kind,
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

static const psx_aggregate_definition_t *gvar_member_aggregate_definition(
    const tag_member_info_t *member) {
  const psx_type_t *type = ps_tag_member_decl_type(member);
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type && ps_type_is_tag_aggregate(type)
             ? type->aggregate_definition
             : NULL;
}

static int gvar_walk_struct_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                        int tag_scope_depth_p1,
                                        const psx_aggregate_definition_t *definition,
                                        global_var_t *gv, gvar_init_cursor_t *cur,
                                        long long base_offset, int struct_size,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user) {
  if (!cur) return 1;
  int prev_end = 0;
  gvar_aggregate_member_iter_t iter =
      gvar_aggregate_member_iter(tag_kind, tag_name, tag_len,
                                 tag_scope_depth_p1, definition);
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
      if (!gvar_init_cursor_pack_bitfield_unit(
              tag_kind, tag_name, tag_len, definition,
              ordinal, cur, &unit)) {
        return 0;
      }
      ops->bitfield_unit(user, &unit, base_offset);
      gvar_aggregate_member_iter_set_next(&iter, unit.last_member_index + 1);
      prev_end = unit.offset + unit.size;
      continue;
    }
    int member_value_size = ps_tag_member_decl_value_size(&mi);
    int member_storage_size = ps_tag_member_decl_storage_size(&mi);
    int member_array_count = ps_tag_member_decl_array_count(&mi);
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    ps_tag_member_decl_tag_identity(&mi, &member_tag_kind, &member_tag_name,
                                     &member_tag_len, NULL);
    const psx_aggregate_definition_t *member_definition =
        gvar_member_aggregate_definition(&mi);
    if (member_array_count > 0) {
      if (ps_tag_member_is_tag_aggregate(&mi)) {
        for (int k = 0; k < member_array_count; k++) {
          if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
          int elem_start_idx = gvar_init_cursor_index(cur);
          long long elem_off = base_offset + mi.offset + (long long)k * member_value_size;
          int ok = ps_tag_member_is_union_aggregate(&mi)
              ? gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                            0, member_definition,
                                            gv, cur, elem_off, member_value_size,
                                            ops, user)
              : gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                             0, member_definition,
                                             gv, cur, elem_off, member_value_size,
                                             ops, user);
          if (!ok) return 0;
          gvar_init_cursor_consume_tag_zero_padding(member_tag_kind, member_tag_name,
                                                    member_tag_len, member_definition, cur,
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
    if (ps_tag_member_is_struct_aggregate(&mi)) {
      int member_start_idx = gvar_init_cursor_index(cur);
      if (!gvar_walk_struct_initializer(member_tag_kind, member_tag_name,
                                        member_tag_len, 0, member_definition,
                                        gv, cur, base_offset + mi.offset,
                                        member_value_size,
                                        ops, user)) {
        return 0;
      }
      gvar_init_cursor_consume_tag_zero_padding(member_tag_kind, member_tag_name,
                                                member_tag_len, member_definition, cur,
                                                member_start_idx);
      prev_end = mi.offset + member_value_size;
      continue;
    }
    if (ps_tag_member_is_union_aggregate(&mi)) {
      if (!gvar_walk_union_initializer(member_tag_kind, member_tag_name,
                                       member_tag_len, 0, member_definition,
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
                                       const psx_aggregate_definition_t *definition,
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
                                            tag_scope_depth_p1, definition, gv,
                                            gvar_init_cursor_index(cur), &mi)) {
    if (ops && ops->padding) {
      gvar_walk_emit_padding(ops, user, base_offset, union_size);
      return 1;
    }
    return 0;
  }
  int member_value_size = ps_tag_member_decl_value_size(&mi);
  int member_storage_size = ps_tag_member_decl_storage_size(&mi);
  int member_array_count = ps_tag_member_decl_array_count(&mi);
  int emitted = member_array_count > 0 ? member_storage_size : member_value_size;
  token_kind_t member_tag_kind = TK_EOF;
  char *member_tag_name = NULL;
  int member_tag_len = 0;
  ps_tag_member_decl_tag_identity(&mi, &member_tag_kind, &member_tag_name,
                                   &member_tag_len, NULL);
  const psx_aggregate_definition_t *member_definition =
      gvar_member_aggregate_definition(&mi);
  if (mi.offset > 0) gvar_walk_emit_padding(ops, user, base_offset, mi.offset);
  if (mi.bit_width > 0) {
    if (!gvar_walk_require_bitfield_member(ops)) return 0;
    int slot = gvar_init_cursor_advance(cur);
    ops->bitfield_member(user, &mi, slot, base_offset + mi.offset);
    gvar_init_cursor_consume_tag_zero_padding(tag_kind, tag_name, tag_len,
                                              definition,
                                              cur, start_idx);
    if (mi.offset + member_value_size < union_size) {
      gvar_walk_emit_padding(ops, user, base_offset + mi.offset + member_value_size,
                             union_size - (mi.offset + member_value_size));
    }
    return 1;
  }
  if (member_array_count > 0) {
    if (ps_tag_member_is_tag_aggregate(&mi)) {
      for (int k = 0; k < member_array_count; k++) {
        if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
        long long elem_off = base_offset + mi.offset + (long long)k * member_value_size;
        int ok = ps_tag_member_is_struct_aggregate(&mi)
            ? gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                           0, member_definition,
                                           gv, cur, elem_off, member_value_size,
                                           ops, user)
            : gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                          0, member_definition,
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
  if (ps_tag_member_is_tag_aggregate(&mi)) {
    int ok = ps_tag_member_is_struct_aggregate(&mi)
        ? gvar_walk_struct_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                       0, member_definition,
                                       gv, cur, base_offset + mi.offset,
                                       member_value_size, ops, user)
        : gvar_walk_union_initializer(member_tag_kind, member_tag_name, member_tag_len,
                                      0, member_definition,
                                      gv, cur, base_offset + mi.offset,
                                      member_value_size, ops, user);
    if (!ok) return 0;
    gvar_init_cursor_consume_tag_zero_padding(tag_kind, tag_name, tag_len,
                                              definition,
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
                                            definition,
                                            cur, start_idx);
  if (mi.offset + member_value_size < union_size) {
    gvar_walk_emit_padding(ops, user, base_offset + mi.offset + member_value_size,
                           union_size - (mi.offset + member_value_size));
  }
  return 1;
}

int ps_gvar_walk_aggregate_initializer(global_var_t *gv, long long base_offset,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user) {
  if (!ps_gvar_is_tag_aggregate(gv)) return 0;
  gvar_aggregate_layout_t layout = gvar_aggregate_layout(gv);
  gvar_init_cursor_t cur = gvar_init_cursor(gv);
  if (!layout.is_array) {
    return layout.is_union
        ? gvar_walk_union_initializer(layout.tag_kind, layout.tag_name,
                                      layout.tag_len, layout.tag_scope_depth_p1,
                                      layout.definition,
                                      gv, &cur, base_offset, layout.type_size,
                                      ops, user)
        : gvar_walk_struct_initializer(layout.tag_kind, layout.tag_name,
                                       layout.tag_len, layout.tag_scope_depth_p1,
                                       layout.definition,
                                       gv, &cur, base_offset, layout.type_size,
                                       ops, user);
  }
  for (int e = 0; e < layout.elem_count; e++) {
    if (!gvar_init_cursor_has(&cur) && !gvar_walk_needs_padding(ops)) break;
    long long elem_off = base_offset + (long long)e * layout.elem_size;
    if (layout.is_union) {
      if (!gvar_walk_union_initializer(layout.tag_kind, layout.tag_name,
                                       layout.tag_len, layout.tag_scope_depth_p1,
                                       layout.definition,
                                       gv, &cur, elem_off, layout.elem_size,
                                       ops, user)) {
        return 0;
      }
    } else {
      int elem_start_idx = gvar_init_cursor_index(&cur);
      if (!gvar_walk_struct_initializer(layout.tag_kind, layout.tag_name,
                                        layout.tag_len, layout.tag_scope_depth_p1,
                                        layout.definition,
                                        gv, &cur, elem_off, layout.elem_size,
                                        ops, user)) {
        return 0;
      }
      gvar_init_cursor_consume_tag_zero_padding(layout.tag_kind, layout.tag_name,
                                                layout.tag_len, layout.definition,
                                                &cur, elem_start_idx);
    }
  }
  return 1;
}

int ps_gvar_initializer_element_size(const global_var_t *gv, int fallback_size) {
  if (ps_gvar_is_array(gv)) {
    psx_type_t *type = gvar_decl_type_view(gv);
    int leaf_elem = type_array_leaf_element_size(type);
    if (leaf_elem > 0) return leaf_elem;
    int elem = ps_gvar_array_element_size(gv);
    if (elem > 0) return elem;
  }
  return fallback_size;
}

int ps_gvar_initializer_element_count(const global_var_t *gv, int fallback_size) {
  if (gv && !ps_gvar_is_array(gv)) return gv->has_init ? 1 : 0;
  int elem = ps_gvar_initializer_element_size(gv, fallback_size);
  psx_type_t *type = gvar_decl_type_view(gv);
  int size = ps_type_sizeof(type);
  if (size <= 0) size = ps_gvar_storage_size(gv, fallback_size);
  return elem > 0 ? (size + elem - 1) / elem : 0;
}

psx_gvar_init_slot_t ps_gvar_init_slot_view(const global_var_t *gv, int idx) {
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
  psx_gvar_view_t view = ps_gvar_view(gv);
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
         ps_gvar_init_slot_is_plain_zero(cur->gv, cur->index)) {
    cur->index++;
    consumed++;
  }
  return consumed;
}

static int gvar_definition_flat_slot_count(
    const psx_aggregate_definition_t *definition);

static int gvar_member_flat_slot_count(const tag_member_info_t *member) {
  if (!member || ps_tag_member_is_unnamed_struct(member)) return 0;
  int per = 1;
  const psx_aggregate_definition_t *definition =
      gvar_member_aggregate_definition(member);
  if (definition) per = gvar_definition_flat_slot_count(definition);
  int count = ps_tag_member_decl_array_count(member);
  return count > 0 ? count * per : per;
}

static int gvar_definition_flat_slot_count(
    const psx_aggregate_definition_t *definition) {
  if (!definition || definition->member_count <= 0) return 1;
  int slots = 0;
  int union_max_bytes = -1;
  psx_tag_flat_cover_state_t cover_state;
  ps_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < definition->member_count; i++) {
    const tag_member_info_t *member = &definition->members[i];
    int member_slots = gvar_member_flat_slot_count(member);
    if (definition->tag_kind == TK_UNION) {
      int bytes = ps_tag_member_decl_storage_size(member);
      if (bytes > union_max_bytes ||
          (bytes == union_max_bytes && member_slots > slots)) {
        union_max_bytes = bytes;
        slots = member_slots;
      }
      continue;
    }
    if (ps_tag_member_is_unnamed_struct(member) ||
        ps_tag_flat_cover_state_covers(&cover_state, member))
      continue;
    slots += member_slots;
    ps_tag_flat_cover_state_note(
        &cover_state, definition->tag_kind,
        definition->tag_name, definition->tag_len, member);
  }
  return slots > 0 ? slots : 1;
}

static int gvar_init_cursor_consume_tag_zero_padding(token_kind_t tag_kind, char *tag_name,
                                                     int tag_len,
                                                     const psx_aggregate_definition_t *definition,
                                                     gvar_init_cursor_t *cur,
                                                     int start_idx) {
  return gvar_init_cursor_consume_plain_zero_padding(
      cur, start_idx,
      definition ? gvar_definition_flat_slot_count(definition)
                 : ps_tag_flat_slot_count(tag_kind, tag_name, tag_len));
}

unsigned long long ps_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  unsigned long long mask = bit_width >= 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
  return ((unsigned long long)slot.value & mask) << bit_offset;
}

static int gvar_init_cursor_pack_bitfield_unit(token_kind_t tag_kind, char *tag_name,
                                               int tag_len,
                                               const psx_aggregate_definition_t *definition,
                                               int member_index,
                                               gvar_init_cursor_t *cur,
                                               psx_gvar_bitfield_unit_t *out) {
  if (!cur || !out) return 0;
  tag_member_info_t first = {0};
  int n_members = definition
                      ? definition->member_count
                      : ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  if (definition && member_index >= 0 && member_index < n_members)
    first = definition->members[member_index];
  else if (!ps_ctx_get_tag_member_info(
               tag_kind, tag_name, tag_len, member_index, &first))
    return 0;
  if (first.bit_width <= 0) return 0;
  int unit_off = first.offset;
  int unit_size = ps_tag_member_decl_value_size(&first);
  unsigned long long packed = 0;
  int m = member_index;
  int last = member_index;
  while (m < n_members && gvar_init_cursor_has(cur)) {
    tag_member_info_t mi = {0};
    if (definition)
      mi = definition->members[m];
    else if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, m, &mi))
      break;
    if (mi.bit_width <= 0 || mi.offset != unit_off) break;
    packed |= ps_gvar_init_slot_bitfield_bits(cur->gv, cur->index,
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

void ps_gvar_init_slots_alloc(global_var_t *gv, int cap, int with_fvalues) {
  if (!gv || cap <= 0) return;
  gv->init_values = calloc((size_t)cap, sizeof(long long));
  gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
  gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
  gv->init_union_ordinals = malloc((size_t)cap * sizeof(int));
  if (with_fvalues) gv->init_fvalues = calloc((size_t)cap, sizeof(double));
  for (int i = 0; i < cap; i++) ps_gvar_init_slot_set_ordinal(gv, i, -1);
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
      ps_gvar_init_slot_clear(gv, i);
    }
    *cap = new_cap;
  }
}

void psx_gvar_init_slots_pad_zeros(global_var_t *gv, int *cap, int total_slots) {
  if (!gv || !cap) return;
  psx_gvar_init_slots_ensure_capacity(gv, cap, total_slots);
  while (gv->init_count < total_slots) {
    ps_gvar_init_slot_clear(gv, gv->init_count);
    gv->init_count++;
  }
}

typedef struct {
  global_var_t *gv;
  int idx;
} gvar_string_units_write_ctx_t;

static void write_gvar_string_unit(uint32_t unit, void *user) {
  gvar_string_units_write_ctx_t *ctx = user;
  ps_gvar_init_slot_write(ctx->gv, ctx->idx++, (long long)unit, 0.0, NULL, 0);
}

int ps_gvar_init_slots_write_string_units(global_var_t *gv, int start_idx,
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

void ps_gvar_init_slot_clear(global_var_t *gv, int idx) {
  if (!gv || idx < 0) return;
  if (gv->init_values) gv->init_values[idx] = 0;
  if (gv->init_value_symbols) gv->init_value_symbols[idx] = NULL;
  if (gv->init_value_symbol_lens) gv->init_value_symbol_lens[idx] = 0;
  if (gv->init_union_ordinals) gv->init_union_ordinals[idx] = -1;
  if (gv->init_fvalues) gv->init_fvalues[idx] = 0.0;
}

void ps_gvar_init_slot_write(global_var_t *gv, int idx, long long value,
                              double fvalue, char *symbol, int symbol_len) {
  if (!gv || idx < 0) return;
  if (gv->init_values) gv->init_values[idx] = value;
  if (gv->init_value_symbols) gv->init_value_symbols[idx] = symbol;
  if (gv->init_value_symbol_lens) gv->init_value_symbol_lens[idx] = symbol_len;
  if (gv->init_fvalues) gv->init_fvalues[idx] = fvalue;
}

void ps_gvar_init_slot_write_fp_sentinel(global_var_t *gv, int idx,
                                          tk_float_kind_t fp_kind, int fp_size) {
  if (!gv || idx < 0 || fp_kind == TK_FLOAT_KIND_NONE) return;
  if (gv->init_value_symbols) gv->init_value_symbols[idx] = NULL;
  if (gv->init_value_symbol_lens) gv->init_value_symbol_lens[idx] = (fp_size >= 8) ? -3 : -2;
}

void ps_gvar_init_slot_set_ordinal(global_var_t *gv, int idx, int ordinal) {
  if (!gv || idx < 0 || !gv->init_union_ordinals) return;
  gv->init_union_ordinals[idx] = ordinal;
}

tk_float_kind_t ps_gvar_init_slot_fp_kind(const global_var_t *gv, int idx) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  if (slot.fp_sentinel_kind != TK_FLOAT_KIND_NONE) return slot.fp_sentinel_kind;
  return TK_FLOAT_KIND_NONE;
}

int ps_gvar_init_slot_is_plain_zero(const global_var_t *gv, int idx) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  if (!slot.in_range) return 1;
  return slot.symbol == NULL && slot.symbol_len == 0 && slot.value == 0 && slot.fvalue == 0.0;
}

int ps_gvar_union_init_slot_fp_size(const global_var_t *gv, int idx) {
  tk_float_kind_t fp_kind = ps_gvar_init_slot_fp_kind(gv, idx);
  if (fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 0;
}

int ps_gvar_union_init_slot_ordinal(const global_var_t *gv, int idx) {
  if (!gv) return -1;
  if (idx >= 0 && idx < gv->init_count &&
      gv->init_union_ordinals && gv->init_union_ordinals[idx] >= 0) {
    return gv->init_union_ordinals[idx];
  }
  return gv->union_init_ordinal;
}

static int tag_member_fp_size(const tag_member_info_t *mi) {
  tk_float_kind_t fp_kind = ps_tag_member_decl_fp_kind(mi);
  return fp_kind == TK_FLOAT_KIND_FLOAT ? 4
       : fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 0;
}

static const psx_type_t *tag_member_direct_tag_leaf_from_type(const tag_member_info_t *mi) {
  const psx_type_t *type = ps_tag_member_decl_type(mi);
  if (!type || type->kind == PSX_TYPE_POINTER) return NULL;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return ps_type_is_tag_aggregate(type) ? type : NULL;
}

int ps_tag_member_is_struct_aggregate(const tag_member_info_t *mi) {
  const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
  return leaf && leaf->kind == PSX_TYPE_STRUCT;
}

int ps_tag_member_is_union_aggregate(const tag_member_info_t *mi) {
  const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
  return leaf && leaf->kind == PSX_TYPE_UNION;
}

int ps_tag_member_is_tag_aggregate(const tag_member_info_t *mi) {
  return ps_tag_member_is_struct_aggregate(mi) ||
         ps_tag_member_is_union_aggregate(mi);
}

int ps_tag_member_is_unnamed_struct(const tag_member_info_t *mi) {
  return mi && mi->len == 0 && ps_tag_member_is_struct_aggregate(mi);
}

int ps_tag_member_is_unnamed_union(const tag_member_info_t *mi) {
  return mi && mi->len == 0 && ps_tag_member_is_union_aggregate(mi);
}

int ps_tag_member_is_unnamed_aggregate(const tag_member_info_t *mi) {
  return ps_tag_member_is_unnamed_struct(mi) ||
         ps_tag_member_is_unnamed_union(mi);
}

void ps_tag_flat_cover_state_init(psx_tag_flat_cover_state_t *state) {
  if (!state) return;
  state->covered_union_off = 0;
  state->covered_union_size = 0;
}

int ps_tag_flat_cover_state_covers(const psx_tag_flat_cover_state_t *state,
                                    const tag_member_info_t *mi) {
  if (!state || !mi || state->covered_union_size <= 0) return 0;
  return mi->offset >= state->covered_union_off &&
         mi->offset < state->covered_union_off + state->covered_union_size;
}

void ps_tag_flat_cover_state_note(psx_tag_flat_cover_state_t *state,
                                   token_kind_t tag_kind, char *tag_name, int tag_len,
                                   const tag_member_info_t *mi) {
  if (!state || !mi) return;
  if (ps_tag_member_is_unnamed_union(mi)) {
    state->covered_union_off = mi->offset;
    state->covered_union_size = ps_tag_member_decl_storage_size(mi);
    return;
  }
  int cover_off = 0;
  int cover_size = 0;
  if (ps_tag_find_unnamed_union_covering_offset(tag_kind, tag_name, tag_len,
                                                 0, mi->offset,
                                                 &cover_off, &cover_size)) {
    state->covered_union_off = cover_off;
    state->covered_union_size = cover_size;
  }
}

int ps_tag_find_unnamed_union_covering_offset(token_kind_t tag_kind, char *tag_name, int tag_len,
                                               int base_off, int target_off,
                                               int *out_off, int *out_size) {
  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (!ps_tag_member_is_unnamed_aggregate(&mi)) continue;
    int start = base_off + mi.offset;
    int member_storage_size = ps_tag_member_decl_storage_size(&mi);
    int end = start + member_storage_size;
    if (target_off < start || target_off >= end) continue;
    if (ps_tag_member_is_union_aggregate(&mi)) {
      if (out_off) *out_off = start;
      if (out_size) *out_size = member_storage_size;
      return 1;
    }
    if (ps_tag_member_is_struct_aggregate(&mi)) {
      token_kind_t child_kind = TK_EOF;
      char *child_name = NULL;
      int child_len = 0;
      ps_tag_member_decl_tag_identity(&mi, &child_kind, &child_name, &child_len, NULL);
      if (ps_tag_find_unnamed_union_covering_offset(child_kind, child_name, child_len,
                                                     start, target_off, out_off, out_size)) {
        return 1;
      }
    }
  }
  return 0;
}

int ps_tag_member_flat_slots(const tag_member_info_t *mi) {
  if (ps_tag_member_is_unnamed_struct(mi)) return 0;
  int per = 1;
  if (ps_tag_member_is_tag_aggregate(mi)) {
    const psx_type_t *leaf = tag_member_direct_tag_leaf_from_type(mi);
    token_kind_t tag_kind = leaf ? leaf->tag_kind : mi->tag_kind;
    char *tag_name = leaf ? leaf->tag_name : mi->tag_name;
    int tag_len = leaf ? leaf->tag_len : mi->tag_len;
    per = ps_tag_flat_slot_count(tag_kind, tag_name, tag_len);
  }
  int count = ps_tag_member_decl_array_count(mi);
  return count > 0 ? count * per : per;
}

int ps_tag_member_elem_flat_slots(const tag_member_info_t *mi) {
  if (!mi) return 1;
  int total = ps_tag_member_flat_slots(mi);
  int count = ps_tag_member_decl_array_count(mi);
  if (count > 0) {
    int per = total / count;
    return per > 0 ? per : 1;
  }
  return total > 0 ? total : 1;
}

int ps_tag_member_subscript_stride_slots(const tag_member_info_t *mi) {
  int per = ps_tag_member_elem_flat_slots(mi);
  if (!mi || mi->arr_ndim <= 1) return per;
  for (int i = 1; i < mi->arr_ndim; i++) {
    int dim = mi->arr_dims[i];
    if (dim > 0) per *= dim;
  }
  return per > 0 ? per : 1;
}

int ps_tag_flat_slot_count(token_kind_t tag_kind, char *tag_name, int tag_len) {
  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int slots = 0;
  int union_max_bytes = 0;
  psx_tag_flat_cover_state_t cover_state;
  ps_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (tag_kind == TK_UNION) {
      int ms = ps_tag_member_flat_slots(&mi);
      int bytes = ps_tag_member_decl_storage_size(&mi);
      if (bytes > union_max_bytes || (bytes == union_max_bytes && ms > slots)) {
        union_max_bytes = bytes;
        slots = ms;
      }
      continue;
    }
    if (ps_tag_member_is_unnamed_struct(&mi)) continue;
    if (ps_tag_flat_cover_state_covers(&cover_state, &mi)) continue;
    slots += ps_tag_member_flat_slots(&mi);
    ps_tag_flat_cover_state_note(&cover_state, tag_kind, tag_name, tag_len, &mi);
  }
  return slots > 0 ? slots : 1;
}

int ps_tag_member_at_flat_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                int flat_slot, tag_member_info_t *out, int *out_ordinal) {
  if (flat_slot < 0) return 0;
  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int slot = 0;
  psx_tag_flat_cover_state_t cover_state;
  ps_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (ps_tag_member_is_unnamed_struct(&mi)) continue;
    if (ps_tag_flat_cover_state_covers(&cover_state, &mi)) continue;
    int member_slots = ps_tag_member_flat_slots(&mi);
    if (flat_slot < slot + member_slots) {
      if (out) *out = mi;
      if (out_ordinal) *out_ordinal = i;
      return 1;
    }
    ps_tag_flat_cover_state_note(&cover_state, tag_kind, tag_name, tag_len, &mi);
    slot += member_slots;
  }
  return 0;
}

int ps_tag_next_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                              int *ordinal_inout, tag_member_info_t *out) {
  if (!ordinal_inout) return 0;
  int ordinal = *ordinal_inout;
  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  while (ordinal < n) {
    tag_member_info_t mi = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, ordinal, &mi)) {
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

int ps_tag_first_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                               tag_member_info_t *out, int *out_ordinal) {
  int ordinal = 0;
  if (!ps_tag_next_named_member(tag_kind, tag_name, tag_len, &ordinal, out)) return 0;
  if (out_ordinal) *out_ordinal = ordinal - 1;
  return 1;
}

int ps_tag_find_named_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                              char *member_name, int member_len,
                              tag_member_info_t *out, int *out_ordinal) {
  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    if (mi.len == member_len && mi.name &&
        strncmp(mi.name, member_name, (size_t)member_len) == 0) {
      if (out) *out = mi;
      if (out_ordinal) *out_ordinal = i;
      return 1;
    }
  }
  return 0;
}

int ps_tag_select_union_member_for_init_slot(token_kind_t tag_kind, char *tag_name,
                                              int tag_len, const global_var_t *gv,
                                              int idx, tag_member_info_t *mi) {
  if (!mi) return 0;
  int init_fp_size = ps_gvar_union_init_slot_fp_size(gv, idx);
  int selected_fp_size = tag_member_fp_size(mi);
  if (init_fp_size == selected_fp_size) return 0;
  if (init_fp_size == 0 && selected_fp_size == 0) return 0;

  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    tag_member_info_t cand = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &cand)) break;
    int cand_fp_size = tag_member_fp_size(&cand);
    if ((init_fp_size > 0 && cand_fp_size == init_fp_size) ||
        (init_fp_size == 0 && cand_fp_size == 0)) {
      *mi = cand;
      return 1;
    }
  }
  return 0;
}

int ps_tag_union_init_member_for_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       const global_var_t *gv, int idx,
                                       tag_member_info_t *out) {
  return tag_union_init_member_for_slot_scoped(tag_kind, tag_name, tag_len, 0,
                                              NULL, gv, idx, out);
}

static int tag_union_init_member_for_slot_scoped(token_kind_t tag_kind, char *tag_name,
                                                int tag_len, int tag_scope_depth_p1,
                                                const psx_aggregate_definition_t *definition,
                                                const global_var_t *gv, int idx,
                                                tag_member_info_t *out) {
  if (!out) return 0;
  int ordinal = ps_gvar_union_init_slot_ordinal(gv, idx);
  if (definition) {
    if (ordinal < 0 || ordinal >= definition->member_count) return 0;
    *out = definition->members[ordinal];
    int init_fp_size = ps_gvar_union_init_slot_fp_size(gv, idx);
    int selected_fp_size = tag_member_fp_size(out);
    if (init_fp_size != selected_fp_size &&
        !(init_fp_size == 0 && selected_fp_size == 0)) {
      for (int i = 0; i < definition->member_count; i++) {
        tag_member_info_t candidate = definition->members[i];
        int candidate_fp_size = tag_member_fp_size(&candidate);
        if ((init_fp_size > 0 && candidate_fp_size == init_fp_size) ||
            (init_fp_size == 0 && candidate_fp_size == 0)) {
          *out = candidate;
          break;
        }
      }
    }
  } else {
    if (!ctx_get_tag_member_info_scoped(tag_kind, tag_name, tag_len,
                                        tag_scope_depth_p1, ordinal, out))
      return 0;
    ps_tag_select_union_member_for_init_slot(
        tag_kind, tag_name, tag_len, gv, idx, out);
  }
  return 1;
}

int ps_tag_member_designator_slot(token_kind_t tag_kind, char *tag_name, int tag_len,
                                   char *member_name, int member_len, int *out_ordinal) {
  int n = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  int slot = 0;
  int covered_union_slot = -1;
  int covered_union_off = 0;
  int covered_union_size = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!ps_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, i, &mi)) break;
    int in_covered_union = covered_union_slot >= 0 &&
                           mi.offset >= covered_union_off &&
                           mi.offset < covered_union_off + covered_union_size;
    if (mi.len == member_len && mi.name &&
        strncmp(mi.name, member_name, (size_t)member_len) == 0) {
      if (out_ordinal) *out_ordinal = i;
      if (in_covered_union) return covered_union_slot;
      return tag_kind == TK_UNION ? 0 : slot;
    }
    if (ps_tag_member_is_unnamed_struct(&mi)) continue;
    if (ps_tag_member_is_unnamed_union(&mi)) {
      covered_union_slot = slot;
      covered_union_off = mi.offset;
      covered_union_size = ps_tag_member_decl_storage_size(&mi);
      slot += ps_tag_member_flat_slots(&mi);
      continue;
    }
    if (in_covered_union) continue;
    int cover_off = 0;
    int cover_size = 0;
    int has_cover = ps_tag_find_unnamed_union_covering_offset(tag_kind, tag_name, tag_len,
                                                               0, mi.offset,
                                                               &cover_off, &cover_size);
    if (has_cover) {
      covered_union_slot = slot;
      covered_union_off = cover_off;
      covered_union_size = cover_size;
    }
    slot += ps_tag_member_flat_slots(&mi);
  }
  return -1;
}

psx_type_t *ps_gvar_get_decl_type(global_var_t *gv) {
  return gvar_decl_type_consistent(gv);
}

static psx_type_t *type_new_void(void) {
  psx_type_t *type = ps_type_new(PSX_TYPE_VOID);
  type->scalar_kind = TK_VOID;
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
    if (ps_type_is_tag_aggregate(cur)) {
      tag_type = cur;
      break;
    }
  }
  if (!tag_type && ps_type_is_tag_aggregate(type)) {
    tag_type = type;
  }
  if (!tag_type && ps_ctx_is_tag_aggregate_kind(type->tag_kind)) {
    tag_type = type;
    ptr = ps_type_is_pointer(type);
  }
  if (!tag_type || !ps_ctx_is_tag_aggregate_kind(tag_type->tag_kind))
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
  if (tag_view_from_type(ps_node_get_type(node), &typed) &&
      typed.kind != TK_EOF) {
    if (view) *view = typed;
    return 1;
  }
  if (view) *view = typed;
  return 0;
}

static int type_is_integer_like(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER;
}

static int type_result_unsigned(const psx_type_t *type) {
  return type && type->kind != PSX_TYPE_POINTER && ps_type_is_unsigned(type);
}

int ps_node_generic_selection_index(node_generic_selection_t *selection) {
  if (!selection || !selection->control ||
      selection->association_count <= 0) {
    return -1;
  }
  int count = selection->association_count;
  psx_type_t **types = arena_alloc(sizeof(psx_type_t *) * (size_t)count);
  unsigned char *defaults = arena_alloc((size_t)count);
  for (int i = 0; i < count; i++) {
    types[i] = selection->associations[i].type;
    defaults[i] = selection->associations[i].is_default;
  }
  return ps_type_generic_select_index(
      ps_node_get_type(selection->control), types, defaults, count);
}

psx_type_t *ps_node_get_type(node_t *node) {
  return node ? node->type : NULL;
}

void ps_node_bind_type(node_t *node, psx_type_t *type) {
  if (!node) return;
  node->type = type;
}

int ps_node_type_size(node_t *node) {
  return node ? ps_type_sizeof(ps_node_get_type(node)) : 0;
}

int ps_node_storage_type_size(node_t *node) {
  return ps_node_type_size(node);
}

int ps_node_deref_size(node_t *node) {
  return node ? ps_type_deref_size(ps_node_get_type(node)) : 0;
}

int ps_node_is_pointer(node_t *node) {
  return node ? ps_type_is_pointer(ps_node_get_type(node)) : 0;
}

int ps_node_pointer_qual_levels(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return pointer_view_from_node_direct(
             node, NODE_POINTER_QUAL_LEVELS, &value)
             ? value
             : 0;
}

int ps_node_base_deref_size(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return pointer_view_from_node_direct(
             node, NODE_POINTER_BASE_DEREF_SIZE, &value)
             ? value
             : 0;
}

int ps_node_ptr_array_pointee_bytes(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return pointer_view_from_node_direct(
             node, NODE_POINTER_PTR_ARRAY_POINTEE_BYTES, &value)
             ? value
             : 0;
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
  int pointer_size = ps_type_sizeof(type->base);
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
      return ps_type_is_unsigned(type);
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

static int pointee_flag_from_type(const psx_type_t *type, node_pointee_flag_t flag) {
  const psx_type_t *base = type_pointee_value_type(type);
  while (base && type_is_pointer_view_type(base) && base->base)
    base = base->base;
  if (!base) return 0;
  switch (flag) {
    case NODE_POINTEE_UNSIGNED:
      return ps_type_is_unsigned(base);
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

static int pointer_view_from_type(const psx_type_t *type, node_pointer_view_field_t field,
                                  int *value) {
  if (!type_is_pointer_view_type(type)) return 0;
  switch (field) {
    case NODE_POINTER_QUAL_LEVELS:
      {
        int levels = ps_type_pointer_view_structural_qual_levels(type);
        if (levels <= 0) return 0;
        if (value) *value = levels;
      }
      return 1;
    case NODE_POINTER_BASE_DEREF_SIZE:
      {
        int base_deref_size =
            ps_type_pointer_view_structural_base_deref_size(type);
        if (base_deref_size <= 0) return 0;
        if (value) *value = base_deref_size;
      }
      return 1;
    case NODE_POINTER_PTR_ARRAY_POINTEE_BYTES:
      {
        int bytes = ps_type_pointer_view_structural_ptr_array_pointee_bytes(type);
        if (bytes <= 0) return 0;
        if (value) *value = bytes;
      }
      return 1;
    case NODE_POINTER_CONST_MASK:
      if (value)
        *value = (int)ps_type_pointer_view_structural_qual_mask(type, 0);
      return 1;
    case NODE_POINTER_VOLATILE_MASK:
      if (value)
        *value = (int)ps_type_pointer_view_structural_qual_mask(type, 1);
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
  psx_type_t *type = ps_node_get_type(node);
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
            ps_type_pointer_view_vla_strides_remaining(type);
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
  psx_type_t *type = ps_node_get_type(node);
  if (vla_view_from_type(type, field, value)) return 1;
  return 0;
}

static psx_decl_funcptr_sig_t funcptr_sig_from_node(node_t *node) {
  if (!node) return (psx_decl_funcptr_sig_t){0};
  return funcptr_sig_from_type(ps_node_get_type(node));
}

int psx_node_has_funcptr_signature(node_t *node) {
  if (!node) return 0;
  return ps_decl_funcptr_sig_has_payload(funcptr_sig_from_node(node));
}

psx_decl_funcptr_sig_t ps_node_funcptr_sig(node_t *node) {
  return funcptr_sig_from_node(node);
}

psx_decl_funcptr_sig_t ps_lvar_funcptr_sig(const lvar_t *src) {
  return funcptr_sig_from_lvar(src);
}

psx_decl_funcptr_sig_t ps_gvar_funcptr_sig(const global_var_t *src) {
  return funcptr_sig_from_gvar(src);
}

psx_decl_funcptr_sig_t ps_gvar_funcptr_sig_by_name(char *name, int len) {
  return ps_gvar_funcptr_sig(ps_find_global_var(name, len));
}

unsigned short psx_node_funcptr_param_fp_mask(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node).function.callable.signature.param_fp_mask;
}

unsigned short psx_node_funcptr_param_int_mask(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node).function.callable.signature.param_int_mask;
}

int psx_node_funcptr_returns_void(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node).function.callable.return_shape.is_void ? 1 : 0;
}

int psx_node_funcptr_returns_complex(node_t *node) {
  if (!node) return 0;
  return funcptr_sig_from_node(node).function.callable.return_shape.is_complex ? 1 : 0;
}

int psx_node_funcptr_returns_pointee_array(node_t *node) {
  if (!node) return 0;
  psx_decl_funcptr_sig_t sig = funcptr_sig_from_node(node);
  return psx_ret_pointee_array_has_dims(sig.function.callable.return_shape.pointee_array) ? 1 : 0;
}

tk_float_kind_t psx_node_funcptr_ret_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  return funcptr_sig_from_node(node).function.callable.return_shape.fp_kind;
}

static global_var_t *static_local_backing_gvar(const lvar_t *var) {
  if (!var || !var->static_global_name) return NULL;
  return ps_find_global_var(var->static_global_name,
                             var->static_global_name_len);
}

static psx_type_t *static_local_backing_decl_type(const lvar_t *var) {
  global_var_t *backing = static_local_backing_gvar(var);
  return backing ? ps_gvar_get_decl_type(backing) : NULL;
}

unsigned int ps_node_pointer_const_qual_mask(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return pointer_view_from_node_direct(node, NODE_POINTER_CONST_MASK, &value)
             ? (unsigned int)value
             : 0;
}

unsigned int ps_node_pointer_volatile_qual_mask(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return pointer_view_from_node_direct(
             node, NODE_POINTER_VOLATILE_MASK, &value)
             ? (unsigned int)value
             : 0;
}

int ps_node_pointee_is_unsigned(node_t *node) {
  return node ? pointee_flag_from_type(
                    ps_node_get_type(node), NODE_POINTEE_UNSIGNED)
              : 0;
}

int ps_node_atomic_pointer_info(node_t *ptr_arg, int *width, int *is_unsigned) {
  int w = ps_node_deref_size(ptr_arg);
  if (w != 1 && w != 2 && w != 4 && w != 8) w = 4;
  if (width) *width = w;

  int u = 0;
  if (ptr_arg && ptr_arg->kind == ND_ADDR && ptr_arg->lhs) {
    u = ps_node_is_unsigned_type(ptr_arg->lhs) ? 1 : 0;
  } else {
    u = ps_node_pointee_is_unsigned(ptr_arg) ? 1 : 0;
  }
  if (is_unsigned) *is_unsigned = u;
  return ptr_arg != NULL;
}

int ps_node_cast_i64_extension_info(node_t *node, int *target_size,
                                     int *widen_zext_i64, int *needs_i64_extend) {
  if (target_size) *target_size = 0;
  if (widen_zext_i64) *widen_zext_i64 = 0;
  if (needs_i64_extend) *needs_i64_extend = 0;
  if (!node) return 0;

  int sz = ps_type_sizeof(ps_node_get_type(node));
  if (sz <= 0) sz = ps_node_type_size(node);

  int zext = node->widen_zext_i64 ? 1 : 0;
  int extend = (!ps_node_value_is_pointer_like(node) && sz >= 8) ? 1 : 0;
  if (target_size) *target_size = sz;
  if (widen_zext_i64) *widen_zext_i64 = zext;
  if (needs_i64_extend) *needs_i64_extend = extend;
  return 1;
}

int ps_node_pointee_is_bool(node_t *node) {
  return node ? pointee_flag_from_type(
                    ps_node_get_type(node), NODE_POINTEE_BOOL)
              : 0;
}

int ps_node_pointee_is_void(node_t *node) {
  psx_type_t *type = node ? ps_node_get_type(node) : NULL;
  return type && type->kind == PSX_TYPE_POINTER && type->base &&
         type->base->kind == PSX_TYPE_VOID;
}

int ps_node_pointee_is_const_qualified(node_t *node) {
  return node ? pointee_flag_from_type(
                    ps_node_get_type(node), NODE_POINTEE_CONST)
              : 0;
}

int ps_node_pointee_is_volatile_qualified(node_t *node) {
  return node ? pointee_flag_from_type(
                    ps_node_get_type(node), NODE_POINTEE_VOLATILE)
              : 0;
}

static int node_self_is_const_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (type_is_pointer_view_type(type))
    return (ps_type_pointer_view_structural_qual_mask(type, 0) & 1u) ? 1 : 0;
  return type && type->is_const_qualified ? 1 : 0;
}

static int node_self_is_volatile_qualified(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (type_is_pointer_view_type(type))
    return (ps_type_pointer_view_structural_qual_mask(type, 1) & 1u) ? 1 : 0;
  return type && type->is_volatile_qualified ? 1 : 0;
}

int ps_node_is_unsigned_type(node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(node), NODE_SCALAR_UNSIGNED)
              : 0;
}

int ps_node_is_long_long_type(node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(node), NODE_SCALAR_LONG_LONG)
              : 0;
}

int ps_node_is_plain_char_type(node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(node), NODE_SCALAR_PLAIN_CHAR)
              : 0;
}

int ps_node_is_long_double_type(node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(node), NODE_SCALAR_LONG_DOUBLE)
              : 0;
}

tk_float_kind_t ps_node_pointee_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  int value = TK_FLOAT_KIND_NONE;
  return pointer_view_from_node_direct(
             node, NODE_POINTER_POINTEE_FP_KIND, &value)
             ? (tk_float_kind_t)value
             : TK_FLOAT_KIND_NONE;
}

/* pointer-to-VLA (`int (*p)[m]`) の行ストライドスロット (実行時値) のフレームオフセット。
 * 無ければ 0。ポインタ算術 (`p + 1`) のスケールに使う。ND_ADD/SUB は被演算子を辿る。 */
int ps_node_vla_row_stride_frame_off(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return vla_view_from_node_direct(
             node, NODE_VLA_ROW_STRIDE_FRAME_OFF, &value)
             ? value
             : 0;
}

static int node_vla_strides_remaining(node_t *node) {
  if (!node) return 0;
  int value = 0;
  return vla_view_from_node_direct(
             node, NODE_VLA_STRIDES_REMAINING, &value)
             ? value
             : 0;
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

static int node_pointer_stride_from_type(
    const psx_type_t *type, int *inner_stride, int *next_stride,
    int *extra_strides, int *extra_strides_count) {
  return ps_type_pointer_view_stride_metadata(
      type, inner_stride, next_stride, extra_strides, extra_strides_count);
}

static int node_pointer_stride_from_node_direct(node_t *node, int *inner_stride,
                                                int *next_stride, int *extra_strides,
                                                int *extra_strides_count) {
  if (!node) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (node_type_state_stride(node, inner_stride, next_stride, extra_strides,
                             extra_strides_count)) {
    return 1;
  }
  if (node_pointer_stride_from_type(type, inner_stride, next_stride,
                                    extra_strides, extra_strides_count)) {
    return 1;
  }
  return 0;
}

int ps_node_pointer_stride_metadata(node_t *node, int *inner_stride,
                                     int *next_stride, int *extra_strides,
                                     int *extra_strides_count) {
  node_pointer_stride_clear(inner_stride, next_stride, extra_strides, extra_strides_count);
  return node_pointer_stride_from_node_direct(
      node, inner_stride, next_stride, extra_strides, extra_strides_count);
}

void ps_node_get_tag_type(node_t *node, token_kind_t *tag_kind, char **tag_name, int *tag_len, int *is_tag_pointer) {
  node_tag_view_t view = node_tag_view_zero();
  tag_view_from_node_direct(node, &view);
  if (tag_kind) *tag_kind = view.kind;
  if (tag_name) *tag_name = view.name;
  if (tag_len) *tag_len = view.len;
  if (is_tag_pointer) *is_tag_pointer = view.is_pointer;
}

int ps_node_get_tag_scope_depth(node_t *node) {
  if (!node) return -1;
  node_tag_view_t view = node_tag_view_zero();
  tag_view_from_node_direct(node, &view);
  return view.scope_depth_p1 > 0 ? view.scope_depth_p1 - 1 : -1;
}

static int node_is_unsigned(node_t *node) {
  if (!node) return 0;
  if (node->has_unsigned_override) return node->unsigned_override ? 1 : 0;
  return type_result_unsigned(ps_node_get_type(node));
}

static int binary_usual_arith_unsigned(node_t *lhs, node_t *rhs) {
  return type_result_unsigned(ps_type_usual_arithmetic_result(
      ps_node_get_type(lhs), ps_node_get_type(rhs),
      TK_FLOAT_KIND_NONE, 0));
}

int ps_node_integer_promotion_is_unsigned(node_t *node) {
  return ps_type_integer_promotion_is_unsigned(ps_node_get_type(node));
}

tk_float_kind_t ps_node_value_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  psx_type_t *type = ps_node_get_type(node);
  if (type && !ps_type_is_pointer(type) &&
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)) {
    return type->fp_kind != TK_FLOAT_KIND_NONE ? type->fp_kind : TK_FLOAT_KIND_DOUBLE;
  }
  return TK_FLOAT_KIND_NONE;
}

int ps_node_value_is_complex(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (type && !ps_type_is_pointer(type)) return type->kind == PSX_TYPE_COMPLEX;
  return 0;
}

int ps_node_value_is_void(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (type) return type->kind == PSX_TYPE_VOID;
  return 0;
}

int ps_node_integer_value_is_unsigned(node_t *node) {
  psx_type_t *type = ps_node_get_type(node);
  return type_is_integer_like(type) && ps_type_is_unsigned(type);
}

/* Conversion/codegen source signedness. This intentionally preserves legacy
 * operation overrides such as cast-lowered forced signed shifts. */
int ps_node_conversion_value_is_unsigned(node_t *node) {
  return node_is_unsigned(node);
}

/* Source signedness for widening an integer value to i64. */
int ps_node_i64_widen_source_is_unsigned(node_t *node) {
  if (!node) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (!type_is_integer_like(type)) return 0;
  int size = ps_type_sizeof(type);
  if (size <= 0) size = ps_node_type_size(node);
  return size >= 4 && node_is_unsigned(node);
}

/* Full shift operation signedness, including explicit cast-lowering overrides. */
int ps_node_shift_operation_is_unsigned(node_t *node) {
  if (!node || (node->kind != ND_SHL && node->kind != ND_SHR)) return 0;
  return node_is_unsigned(node);
}

int ps_node_usual_arith_operands_is_unsigned(node_t *lhs, node_t *rhs) {
  return binary_usual_arith_unsigned(lhs, rhs);
}

int ps_node_usual_arith_is_unsigned(node_t *node) {
  if (!node) return 0;
  if (node->has_unsigned_override) return node_is_unsigned(node);
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
      return ps_node_usual_arith_operands_is_unsigned(node->lhs, node->rhs);
    case ND_TERNARY: {
      psx_type_t *type = ps_node_get_type(node);
      return type_result_unsigned(type);
    }
    default:
      return type_result_unsigned(ps_node_get_type(node));
  }
}

/* node の符号フラグを設定する (node_is_unsigned が読むフィールドに一致させる)。
 * `(int)u` / `(unsigned)i` キャストで結果の符号を確定するのに使う。 */
void ps_node_set_unsigned(node_t *node, int is_unsigned) {
  if (!node) return;
  node->has_unsigned_override = 1;
  node->unsigned_override = is_unsigned ? 1 : 0;
}

node_t *psx_node_new_raw_binary(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

int ps_node_binary_type_op(
    node_kind_t kind, psx_type_binary_op_t *op) {
  if (!op) return 0;
  switch (kind) {
    case ND_COMMA: *op = PSX_TYPE_BINARY_COMMA; return 1;
    case ND_ADD: *op = PSX_TYPE_BINARY_ADD; return 1;
    case ND_SUB: *op = PSX_TYPE_BINARY_SUB; return 1;
    case ND_MUL: *op = PSX_TYPE_BINARY_MUL; return 1;
    case ND_DIV: *op = PSX_TYPE_BINARY_DIV; return 1;
    case ND_MOD: *op = PSX_TYPE_BINARY_MOD; return 1;
    case ND_BITAND: *op = PSX_TYPE_BINARY_BITAND; return 1;
    case ND_BITXOR: *op = PSX_TYPE_BINARY_BITXOR; return 1;
    case ND_BITOR: *op = PSX_TYPE_BINARY_BITOR; return 1;
    case ND_SHL: *op = PSX_TYPE_BINARY_SHL; return 1;
    case ND_SHR: *op = PSX_TYPE_BINARY_SHR; return 1;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      *op = PSX_TYPE_BINARY_COMPARE;
      return 1;
    case ND_LOGAND:
    case ND_LOGOR:
      *op = PSX_TYPE_BINARY_LOGICAL;
      return 1;
    default:
      return 0;
  }
}

node_t *ps_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = psx_node_new_raw_binary(kind, lhs, rhs);
  psx_type_binary_op_t op;
  psx_type_t *type = ps_node_binary_type_op(kind, &op)
                         ? ps_type_binary_result(
                               op, ps_node_get_type(lhs),
                               ps_node_get_type(rhs))
                         : NULL;
  if (type) {
    ps_node_bind_type(node, type);
  }
  return node;
}

node_t *ps_node_new_shift_trunc_extend(node_t *operand, int left_shift, int is_unsigned) {
  node_t *shl = ps_node_new_binary(ND_SHL, operand, ps_node_new_num(left_shift));
  node_t *shr = ps_node_new_binary(ND_SHR, shl, ps_node_new_num(left_shift));
  ps_node_set_unsigned(shl, is_unsigned ? 1 : 0);
  ps_node_set_unsigned(shr, is_unsigned ? 1 : 0);
  return shr;
}

node_t *ps_node_new_num(long long val) {
  node_num_t *node = arena_alloc(sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->base.type = ps_type_new_integer(TK_INT, 4, 0);
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
      offset, NULL, ps_type_new_integer(TK_INT, 8, 0));
}

node_t *ps_node_new_lvar_typed(int offset, int type_size) {
  int size = type_size > 0 ? type_size : 8;
  return (node_t *)new_lvar_symbol_node(
      offset, NULL, ps_type_new_integer(TK_INT, size, 0));
}

node_t *ps_node_new_lvar_typed_at_for(lvar_t *owner, int offset, int type_size) {
  psx_type_t *type = NULL;
  if (owner) {
    psx_type_t *owner_type = ps_lvar_get_decl_type(owner);
    int rel = offset - owner->offset;
    if (rel == 0 && ps_type_sizeof(owner_type) == type_size)
      type = owner_type;
    else if (rel >= 0 && owner->elem_size > 0 &&
             (rel % owner->elem_size) == 0)
      type = type_array_element_type_for_size(owner_type, type_size);
  }
  if (!type) type = ps_type_new_integer(TK_INT, type_size > 0 ? type_size : 8, 0);
  return (node_t *)new_lvar_symbol_node(offset, owner, type);
}

node_t *ps_node_new_lvar_type_at_for(lvar_t *owner, int offset,
                                      psx_type_t *type) {
  return (node_t *)new_lvar_symbol_node(offset, owner, type);
}

node_t *psx_node_new_lvar_scalar_slot_at(int offset, int type_size,
                                         tk_float_kind_t fp_kind, int is_bool) {
  psx_type_t *type = fp_kind != TK_FLOAT_KIND_NONE
                         ? ps_type_new_float(fp_kind, type_size)
                     : is_bool
                         ? ps_type_new_integer(TK_BOOL, type_size, 1)
                         : ps_type_new_integer(TK_INT, type_size, 0);
  return (node_t *)new_lvar_symbol_node(offset, NULL, type);
}

node_t *psx_node_new_lvar_fp_slot_at(int offset, int type_size, tk_float_kind_t fp_kind) {
  return psx_node_new_lvar_scalar_slot_at(offset, type_size, fp_kind, 0);
}

node_t *ps_node_new_lvar_fp_slot_for(lvar_t *owner, int offset, int type_size) {
  tk_float_kind_t fp_kind = ps_lvar_fp_kind(owner);
  psx_type_t *type = fp_kind != TK_FLOAT_KIND_NONE
                         ? ps_type_new_float(fp_kind, type_size)
                         : ps_type_new_integer(TK_INT, type_size, 0);
  return (node_t *)new_lvar_symbol_node(offset, owner, type);
}

node_t *ps_node_new_param_placeholder(psx_type_t *type) {
  return (node_t *)new_lvar_symbol_node(0, NULL, type);
}

node_t *ps_node_new_unsigned_lvar_typed(int offset, int type_size) {
  return (node_t *)new_lvar_symbol_node(
      offset, NULL, ps_type_new_integer(TK_UNSIGNED, type_size, 1));
}

node_t *psx_node_new_lvar_for(lvar_t *var) {
  psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, type);
}

node_t *psx_node_new_lvar_typed_for(lvar_t *var, int type_size) {
  (void)type_size;
  psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, type);
}

static int lvar_public_storage_size_or_elem(const lvar_t *var) {
  int elem_size = ps_lvar_elem_size(var, 0);
  return ps_lvar_storage_size(var, elem_size);
}

node_t *psx_node_new_lvar_object_ref_for(lvar_t *var) {
  return psx_node_new_lvar_typed_for(var, lvar_public_storage_size_or_elem(var));
}

node_t *ps_node_new_lvar_expr_ref_for(lvar_t *var, int is_pointer) {
  (void)is_pointer;
  psx_type_t *decl_type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(var ? var->offset : 0, var, decl_type);
}

node_t *psx_node_new_lvar_identifier_ref_for(lvar_t *var) {
  if (var && var->is_static_local && var->static_global_name) {
    int sz = var->size > 0 ? var->size : var->elem_size;
    return psx_node_new_static_local_gvar_for(var, sz);
  }

  return psx_node_new_lvar_for(var);
}

node_t *psx_node_new_vla_decay_ref_for(lvar_t *var) {
  psx_type_t *array_type = var ? ps_lvar_get_decl_type(var) : NULL;
  psx_type_t *decay_type = type_decay_array_to_pointer(array_type);
  if (!decay_type) return psx_node_new_lvar_identifier_ref_for(var);
  return (node_t *)new_lvar_symbol_node(var->offset, var, decay_type);
}

node_t *ps_node_new_param_lvar_for(lvar_t *var) {
  psx_type_t *decl_type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(
      var ? var->offset : 0, var, decl_type);
}

node_t *ps_node_new_array_elem_lvar_for(lvar_t *var, int idx) {
  psx_type_t *elem_type =
      var ? type_array_leaf_element_type(ps_lvar_get_decl_type(var)) : NULL;
  int canonical_elem_size = ps_type_sizeof(elem_type);
  int elem_size = canonical_elem_size > 0 ? canonical_elem_size
                  : (var ? var->elem_size : 0);
  int offset = var ? var->offset + idx * elem_size : 0;
  if (!elem_type)
    elem_type = ps_type_new_integer(TK_INT, elem_size > 0 ? elem_size : 4,
                                     0);
  return (node_t *)new_lvar_symbol_node(offset, var, elem_type);
}

static node_t *annotate_explicit_type(node_t *node, psx_type_t *type) {
  if (node && type) ps_node_bind_type(node, type);
  return node;
}

node_t *ps_node_new_fp_to_int_cast(node_t *operand, int width, psx_type_t *cast_type) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_FP_TO_INT;
  node->lhs = operand;
  if (!cast_type) {
    int type_size = width == 8 ? 8 : 4;
    cast_type = ps_type_new_integer(TK_INT, type_size, 0);
  }
  return annotate_explicit_type(node, cast_type);
}

node_t *ps_node_new_int_to_fp_cast(node_t *operand, tk_float_kind_t target,
                                    psx_type_t *cast_type) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_INT_TO_FP;
  node->lhs = operand;
  if (!cast_type) {
    cast_type = ps_type_new_float(target, target == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  return annotate_explicit_type(node, cast_type);
}

node_t *ps_node_new_integer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         int type_size, int is_unsigned,
                                         int is_long_long) {
  return ps_node_new_integer_cast_result_ex(operand, cast_type, type_size, is_unsigned,
                                             is_long_long, 0, 0);
}

node_t *ps_node_new_integer_cast_result_ex(node_t *operand, psx_type_t *cast_type,
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
    cast_type = ps_type_new_integer(scalar_kind, type_size, is_unsigned);
    cast_type->is_long_long = is_long_long ? 1 : 0;
  }
  return annotate_explicit_type(wrap, cast_type);
}

node_t *ps_node_new_i64_to_i32_trunc_cast(node_t *operand, psx_type_t *cast_type,
                                           int is_unsigned) {
  node_t *trunc = ps_node_new_shift_trunc_extend(operand, 32, is_unsigned);
  return ps_node_new_integer_cast_result(trunc, cast_type, 4, is_unsigned, 0);
}

node_t *ps_node_new_pointer_cast_result(node_t *operand, psx_type_t *cast_type,
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
    } else if (ps_ctx_is_tag_aggregate_kind(tag_kind)) {
      int tag_size = ps_ctx_get_tag_size(tag_kind, tag_name, tag_len);
      if (tag_size <= 0) tag_size = elem_size;
      base = ps_type_new_tag(tag_kind, tag_name, tag_len, 0, tag_size);
    } else if (type_kind == TK_FLOAT || type_kind == TK_DOUBLE) {
      tk_float_kind_t fp_kind = type_kind == TK_FLOAT
                                    ? TK_FLOAT_KIND_FLOAT
                                    : TK_FLOAT_KIND_DOUBLE;
      base = ps_type_new_float(fp_kind, elem_size > 0 ? elem_size : 8);
    } else if (type_kind == TK_BOOL) {
      base = ps_type_new_integer(TK_BOOL, elem_size > 0 ? elem_size : 1, 1);
    } else {
      base = ps_type_new_integer(
          is_unsigned ? TK_UNSIGNED : TK_INT,
          elem_size > 0 ? elem_size : 4, is_unsigned);
    }
    int deref_size = elem_size > 0 ? elem_size : 8;
    int base_deref_size = elem_size > 0 ? elem_size : 8;
    cast_type = ps_type_wrap_pointer_levels(
        base, pointer_levels, deref_size, base_deref_size, 0, 0);
  }
  return annotate_explicit_type(wrap, cast_type);
}

node_t *ps_node_new_aggregate_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_t *wrap = arena_alloc(sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  return annotate_explicit_type(wrap, cast_type);
}

node_t *ps_node_new_void_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_t *wrap = arena_alloc(sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  if (!cast_type) cast_type = type_new_void();
  return annotate_explicit_type(wrap, cast_type);
}

node_t *psx_node_new_source_cast(
    node_t *operand, psx_type_name_ref_t type_name) {
  node_source_cast_t *cast = arena_alloc(sizeof(node_num_t));
  cast->base.kind = ND_CAST;
  cast->base.lhs = operand;
  cast->base.is_source_cast = 1;
  cast->type_name = type_name;
  return (node_t *)cast;
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
  psx_type_t *base = ps_node_get_type(operand);
  if (!base) return NULL;
  int deref_size = ps_type_sizeof(base);
  if (deref_size <= 0) deref_size = ps_node_type_size(operand);
  if (deref_size <= 0) deref_size = 8;
  psx_type_t *type = ps_type_new_pointer(base, deref_size);
  int operand_levels = ps_node_pointer_qual_levels(operand);
  type->pointer_qual_levels = operand_levels > 0 ? operand_levels + 1 : 1;
  int operand_base_deref_size = ps_node_base_deref_size(operand);
  type->base_deref_size = operand_base_deref_size > 0 ? operand_base_deref_size
                                                      : deref_size;
  type->pointer_const_qual_mask = ps_node_pointer_const_qual_mask(operand) << 1;
  type->pointer_volatile_qual_mask =
      ps_node_pointer_volatile_qual_mask(operand) << 1;
  return type;
}

static psx_type_t *type_decay_array_to_pointer(psx_type_t *array_type) {
  if (!array_type || array_type->kind != PSX_TYPE_ARRAY || !array_type->base)
    return NULL;
  int elem_size = ps_type_sizeof(array_type->base);
  if (elem_size <= 0) elem_size = ps_type_deref_size(array_type);
  if (elem_size <= 0) elem_size = array_type->elem_size;
  if (elem_size <= 0) elem_size = 8;
  psx_type_t *ptr = ps_type_new_pointer(array_type->base, elem_size);
  if (array_type->base->kind == PSX_TYPE_POINTER) {
    int base_levels = array_type->base->pointer_qual_levels > 0
                          ? array_type->base->pointer_qual_levels
                          : type_pointer_depth(array_type->base);
    ptr->pointer_qual_levels = base_levels + 1;
  }
  ptr->base_deref_size = array_type->base_deref_size > 0
                             ? array_type->base_deref_size
                             : ps_type_deref_size(array_type->base);
  if (ptr->base_deref_size <= 0) ptr->base_deref_size = elem_size;
  ptr->pointee_fp_kind = array_type->pointee_fp_kind;
  ptr->funcptr_sig = ps_decl_funcptr_sig_clone(array_type->funcptr_sig);
  int ptr_array_pointee_bytes =
      ps_type_pointer_view_structural_ptr_array_pointee_bytes(ptr);
  if (ptr_array_pointee_bytes > 0)
    ptr->ptr_array_pointee_bytes = ptr_array_pointee_bytes;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (ps_type_pointer_view_stride_metadata(ptr, &inner_stride, &next_stride,
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
      ps_type_pointer_view_vla_strides_remaining(array_type);
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
      int width = ps_type_sizeof(base);
      sig.function.callable.return_shape.int_width = (unsigned char)(width >= 8 ? 8 : 4);
      break;
    }
    default:
      break;
  }
  return sig;
}

static psx_type_t *type_from_deref_operand(node_t *operand) {
  psx_type_t *type = ps_node_get_type(operand);
  if (!type_is_pointer_view_type(type) || !type->base) return NULL;
  if (!operand || !operand->type)
    psx_type_canonicalize_flat_pointer_to_array(type);
  int pointer_levels = ps_type_pointer_view_structural_qual_levels(type);
  int ptr_array_pointee_bytes =
      ps_type_pointer_view_structural_ptr_array_pointee_bytes(type);
  int has_structural_stride = ps_type_pointer_view_stride_metadata(
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
                                              : ps_type_sizeof(type->base);
    if (ptr_array_pointee_bytes > 0 && type_is_pointer_view_type(type->base)) {
      int pointer_elem_size = ps_type_sizeof(type->base);
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
      int base_size = ps_type_sizeof(type->base);
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
          ps_type_new_array(type->base, array_len, array_size, elem_size, 0);
      if (type_is_pointer_view_type(type->base)) {
        array->pointer_qual_levels =
            type->base->pointer_qual_levels > 0 ? type->base->pointer_qual_levels : 1;
        array->base_deref_size =
            type->base->base_deref_size > 0 ? type->base->base_deref_size
                                            : ps_type_deref_size(type->base);
      } else {
        array->base_deref_size = elem_size;
      }
      array->pointee_fp_kind = type->pointee_fp_kind;
      array->ptr_array_pointee_bytes =
          (type_is_pointer_to_array_type(type->base) ||
           ps_type_is_tag_aggregate(type->base))
              ? ptr_array_pointee_bytes : 0;
      psx_type_copy_pointer_view_stride_metadata(array, type);
      return type_array_with_pointer_element_storage(array);
    }
  }
  if (!type_is_pointer_view_type(type->base) && pointer_levels >= 2) {
    int deref_size = type->base_deref_size > 0 ? type->base_deref_size
                                               : ps_type_sizeof(type->base);
    if (deref_size <= 0) deref_size = type->deref_size;
    if (deref_size <= 0) deref_size = 8;
    psx_type_t *result = ps_type_new_pointer(type->base, deref_size);
    result->pointer_qual_levels = pointer_levels - 1;
    result->base_deref_size = deref_size;
    result->pointer_const_qual_mask =
        ps_type_pointer_view_structural_qual_mask(type, 0) >> 1;
    result->pointer_volatile_qual_mask =
        ps_type_pointer_view_structural_qual_mask(type, 1) >> 1;
    result->pointee_fp_kind = type->pointee_fp_kind;
    result->funcptr_sig =
        ps_decl_funcptr_sig_clone(funcptr_sig_for_deref_result(
            type->funcptr_sig, type->base, result->pointer_qual_levels));
    result->ptr_array_pointee_bytes =
        ps_type_pointer_view_structural_ptr_array_pointee_bytes(type);
    psx_type_copy_pointer_view_stride_metadata(result, type);
    return result;
  }
  return type_array_with_pointer_element_storage(type->base);
}

static psx_type_t *type_normalize_tag_aggregate_size(psx_type_t *type,
                                                     int value_size) {
  if (!ps_type_is_tag_aggregate(type) || value_size <= 0 ||
      value_size >= ps_type_sizeof(type)) {
    return type;
  }
  psx_type_t *tag = ps_type_new_tag(type->tag_kind, type->tag_name,
                                     type->tag_len,
                                     type->tag_scope_depth_p1,
                                     value_size);
  psx_type_copy_common_qualifiers(tag, type);
  tag->funcptr_sig = ps_decl_funcptr_sig_clone(type->funcptr_sig);
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
    int pointer_elem_size = ps_type_sizeof(base_type->base);
    if (pointer_elem_size > 0 && elem_size == pointer_elem_size) {
      return type_with_funcptr_sig(base_type->base, base_type->funcptr_sig);
    }
  }
  if (base_type->kind == PSX_TYPE_POINTER && base_type->base &&
      base_type->base->kind == PSX_TYPE_ARRAY &&
      (!base_type->base->base || base_type->base->base->kind != PSX_TYPE_ARRAY) &&
      ps_type_deref_size(base_type->base) <= ps_type_sizeof(base_type->base->base)) {
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

  int base_elem_size = ps_type_sizeof(view->base);
  int subscript_yields_pointer_element =
      view->base->kind == PSX_TYPE_POINTER &&
      base_elem_size > 0 &&
      ((inner_deref_size > 0 && inner_deref_size == base_elem_size) ||
       (inner_deref_size <= 0 && elem_size == base_elem_size));
  int subscript_yields_tag_element =
      ps_type_is_tag_aggregate(view->base) &&
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
                                           : ps_type_sizeof(view->base);
  if (row_elem_size <= 0) row_elem_size = view->elem_size;
  if (row_elem_size <= 0) return view->base;
  if (view->base->kind == PSX_TYPE_ARRAY &&
      (ps_type_sizeof(view->base) == elem_size ||
       (view->ptr_array_pointee_bytes > 0 &&
        ps_type_sizeof(view->base) == row_elem_size))) {
    return view->base;
  }
  int row_len = elem_size / row_elem_size;
  if (row_len <= 0) row_len = 1;
  psx_type_t *row_base = view->base;
  int leaf_size = ps_type_sizeof(view->base);
  if (view->base && view->base->kind != PSX_TYPE_ARRAY &&
      leaf_size > 0 && row_elem_size > leaf_size &&
      (row_elem_size % leaf_size) == 0) {
    int inner_len = row_elem_size / leaf_size;
    row_base = ps_type_new_array(view->base, inner_len, row_elem_size,
                                  leaf_size, view->is_vla);
    row_base->base_deref_size = view->base_deref_size > 0
                                    ? view->base_deref_size
                                    : leaf_size;
  }
  psx_type_t *row = ps_type_new_array(row_base, row_len, elem_size,
                                       row_elem_size, view->is_vla);
  row->base_deref_size = view->base_deref_size > 0
                             ? view->base_deref_size
                             : row_elem_size;
  row->outer_stride = next_deref_size;
  row->pointee_fp_kind = view->pointee_fp_kind;
  row->funcptr_sig = ps_decl_funcptr_sig_clone(view->funcptr_sig);
  row->ptr_array_pointee_bytes = view->ptr_array_pointee_bytes;
  row->vla_row_stride_frame_off =
      psx_type_pointer_view_vla_row_stride_frame_off(view);
  int view_vla_strides_remaining =
      ps_type_pointer_view_vla_strides_remaining(view);
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

node_t *ps_node_new_gvar_array_addr_for(global_var_t *gv) {
  node_t *addr = new_addr_node(psx_node_new_gvar_array_base_for(gv));
  init_array_addr_canonical_type(addr, ps_gvar_get_decl_type(gv));
  return addr;
}

node_t *psx_node_new_static_local_array_addr_for(lvar_t *var, int gvar_type_size) {
  node_t *addr = new_addr_node(
      psx_node_new_static_local_gvar_for(var, gvar_type_size));
  init_array_addr_canonical_type(addr, static_local_backing_decl_type(var));
  return addr;
}

node_t *ps_node_new_lvar_array_addr_for(lvar_t *var, int is_tag_pointer) {
  (void)is_tag_pointer;
  node_t *addr = new_addr_node(psx_node_new_lvar_for(var));
  init_array_addr_canonical_type(addr, ps_lvar_get_decl_type(var));
  return addr;
}

node_t *ps_node_new_addr_value_for(node_t *operand) {
  node_t *addr = new_addr_node(operand);
  ps_node_bind_type(addr, type_from_address_operand(operand));
  return addr;
}

node_t *ps_node_new_explicit_addr_value_for(node_t *operand) {
  if (!operand || operand->kind != ND_ADDR) return operand;
  node_t *cp = arena_alloc(sizeof(node_t));
  *cp = *operand;
  ps_node_bind_type(cp, type_from_address_operand(operand->lhs));
  cp->type_state = (psx_expr_type_state_t){0};
  cp->is_explicit_addr_expr = 1;
  return cp;
}

node_t *ps_node_new_unary_addr_for(node_t *operand) {
  node_t *node = new_addr_node(operand);
  ps_node_bind_type(node, type_from_address_operand(operand));
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
      !ps_node_pointer_stride_metadata(
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

node_t *ps_node_new_tag_member_deref_for(node_t *addr_base, node_t *base,
                                          const tag_member_info_t *info) {
  if (!info) return NULL;
  node_t *addr = ps_node_new_binary(ND_ADD, addr_base, ps_node_new_num(info->offset));
  node_t *deref = arena_alloc(sizeof(node_t));
  deref->kind = ND_DEREF;
  deref->lhs = addr;
  int mem_size = ps_tag_member_decl_value_size(info);
  int mem_array_len = ps_tag_member_decl_array_count(info);
  int mem_is_ptr = ps_tag_member_decl_is_pointer(info);
  tk_float_kind_t mem_fp_kind = ps_tag_member_decl_fp_kind(info);
  int member_is_const =
      ps_node_pointee_is_const_qualified(base) ||
      (!ps_node_is_pointer(base) && node_self_is_const_qualified(base));
  int member_is_volatile =
      ps_node_pointee_is_volatile_qualified(base) ||
      (!ps_node_is_pointer(base) && node_self_is_volatile_qualified(base));
  deref->type_state.bit_width = (unsigned char)info->bit_width;
  deref->type_state.bit_offset = (unsigned char)info->bit_offset;
  deref->type_state.bit_is_signed = info->bit_is_signed ? 1 : 0;
  psx_decl_funcptr_sig_t member_funcptr_sig = ps_ctx_tag_member_funcptr_sig(info);
  if (ps_decl_funcptr_sig_has_payload(member_funcptr_sig) &&
      member_funcptr_sig.function.callable.return_shape.fp_kind == TK_FLOAT_KIND_NONE &&
      !member_funcptr_sig.function.callable.return_shape.is_data_pointer &&
      mem_fp_kind != TK_FLOAT_KIND_NONE) {
    member_funcptr_sig.function.callable.return_shape.fp_kind = mem_fp_kind;
  }
  psx_type_t *decl_type = (psx_type_t *)ps_tag_member_decl_type(info);
  if (decl_type) {
    psx_type_t *member_type =
        type_with_funcptr_sig_merged(decl_type, member_funcptr_sig);
    ps_node_bind_type(
        deref, type_with_self_qualifiers(
                   member_type, member_is_const, member_is_volatile));
    deref->type_state.is_scalar_ptr_member_lvalue =
        mem_is_ptr && mem_size > 0 && mem_array_len <= 0;
  }
  return deref;
}

node_t *ps_node_new_unary_deref_for(node_t *operand) {
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
  ps_node_bind_type(result, result_type);
  init_unary_deref_expr_state(result, operand);
  return result;
}

node_t *psx_node_new_unary_deref_syntax_for(node_t *operand) {
  node_t *result = arena_alloc(sizeof(node_t));
  result->kind = ND_UNARY_DEREF;
  result->lhs = operand;
  return result;
}

node_t *psx_node_new_subscript_syntax_for(node_t *base, node_t *index) {
  node_t *result = arena_alloc(sizeof(node_t));
  result->kind = ND_SUBSCRIPT;
  result->lhs = base;
  result->rhs = index;
  return result;
}

node_t *ps_node_new_subscript_deref_for(node_t *base, node_t *base_addr,
                                         node_t *scaled_offset,
                                         int elem_size, int inner_deref_size,
                                         int next_deref_size,
                                         const int *extra_strides,
                                         int extra_strides_count) {
  psx_type_t *base_type = ps_node_get_type(base);
  psx_type_t *fixed_result_type = type_from_subscript_base_type(
      base_type, elem_size, inner_deref_size, next_deref_size,
      extra_strides, extra_strides_count);
  int parent_vla_row = ps_node_vla_row_stride_frame_off(base);
  if (base_type && parent_vla_row != 0) {
    int parent_remaining = node_vla_strides_remaining(base);
    psx_type_t *vla_result_type = type_from_vla_subscript_result(
        base_type, fixed_result_type, elem_size, inner_deref_size,
        parent_vla_row, parent_remaining);
    if (vla_result_type) {
      node_t *result = arena_alloc(sizeof(node_t));
      result->kind = ND_DEREF;
      result->lhs = ps_node_new_binary(ND_ADD, base_addr, scaled_offset);
      ps_node_bind_type(result, vla_result_type);
      result->type_state.subscript_uses_base_address =
          inner_deref_size > 0 ? 1 : 0;
      if (parent_remaining > 0) {
        int parent_elem = 0;
        ps_node_pointer_stride_metadata(base, &parent_elem, NULL, NULL, NULL);
        if (parent_elem > 0)
          node_type_state_store_stride(result, parent_elem, parent_elem,
                                       NULL, 0);
      }
      return result;
    }
  }
  if (fixed_result_type && parent_vla_row == 0) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = ps_node_new_binary(ND_ADD, base_addr, scaled_offset);
    ps_node_bind_type(result, fixed_result_type);
    init_subscript_expr_state(result, next_deref_size,
                              extra_strides, extra_strides_count);
    return result;
  }
  psx_type_t *deref_result_type = type_from_deref_operand(base);
  if (base_type && deref_result_type && parent_vla_row == 0) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = ps_node_new_binary(ND_ADD, base_addr, scaled_offset);
    ps_node_bind_type(result, deref_result_type);
    init_subscript_expr_state(result, next_deref_size,
                              extra_strides, extra_strides_count);
    return result;
  }
  if (base_type) {
    node_t *result = arena_alloc(sizeof(node_t));
    result->kind = ND_DEREF;
    result->lhs = ps_node_new_binary(ND_ADD, base_addr, scaled_offset);
    return result;
  }
  node_t *result = arena_alloc(sizeof(node_t));
  result->kind = ND_DEREF;
  result->lhs = ps_node_new_binary(ND_ADD, base_addr, scaled_offset);
  return result;
}

node_t *psx_node_new_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                         int member_type_size, token_kind_t member_tag_kind,
                                         char *member_tag_name, int member_tag_len,
                                         int member_is_tag_pointer) {
  int size = member_type_size > 0 ? member_type_size : 4;
  psx_type_t *type = NULL;
  if (member_tag_kind != TK_EOF) {
    type = ps_type_new_tag(member_tag_kind, member_tag_name, member_tag_len,
                            0, size);
    if (member_is_tag_pointer)
      type = ps_type_new_pointer(type, size);
  } else {
    type = ps_type_new_integer(TK_INT, size, 0);
  }
  return (node_t *)new_lvar_symbol_node(
      (owner ? owner->offset : 0) + member_offset, owner, type);
}

node_t *ps_node_new_tag_member_lvar_ref_for(lvar_t *owner, int member_offset,
                                             const tag_member_info_t *info) {
  psx_type_t *decl_type = (psx_type_t *)ps_tag_member_decl_type(info);
  psx_type_t *member_type = decl_type;
  if (decl_type) {
    member_type = type_with_funcptr_sig_merged(
        decl_type, ps_ctx_tag_member_funcptr_sig(info));
    int owner_is_const = lvar_self_is_const_qualified(owner);
    int owner_is_volatile = lvar_self_is_volatile_qualified(owner);
    member_type = type_with_self_qualifiers(
        member_type, owner_is_const, owner_is_volatile);
  }
  if (!member_type)
    member_type = ps_type_new_integer(
        ps_tag_member_decl_is_bool(info) ? TK_BOOL : TK_INT,
        ps_tag_member_decl_value_size(info),
        ps_tag_member_decl_is_unsigned(info));
  node_lvar_t *node = new_lvar_symbol_node(
      (owner ? owner->offset : 0) + member_offset, owner, member_type);
  if (info && info->bit_width > 0) {
    node->base.type_state.bit_width = (unsigned char)info->bit_width;
    node->base.type_state.bit_offset = (unsigned char)info->bit_offset;
    node->base.type_state.bit_is_signed = info->bit_is_signed ? 1 : 0;
  }
  return (node_t *)node;
}

node_t *ps_node_new_gvar_for(global_var_t *gv) {
  node_gvar_t *node = arena_alloc(sizeof(node_gvar_t));
  node->base.kind = ND_GVAR;
  if (gv) {
    node->base.type = ps_gvar_get_decl_type(gv);
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
    node->base.type = ps_gvar_get_decl_type(gv);
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
    if (!node->base.type) node->base.type = ps_lvar_get_decl_type(var);
    node->name = var->static_global_name;
    node->name_len = var->static_global_name_len;
  }
  return (node_t *)node;
}

lvar_t *ps_node_lvar_symbol(node_t *node) {
  if (!node || node->kind != ND_LVAR) return NULL;
  node_lvar_t *lv = (node_lvar_t *)node;
  return lv->var ? lv->var : psx_decl_find_lvar_by_offset(lv->offset);
}

node_t *ps_node_clone_lvalue_with_lhs(node_t *target, node_t *lhs) {
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
    case ND_UNARY_DEREF:
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

static int node_scalar_ptr_member_lvalue(node_t *node) {
  psx_type_t *type = ps_node_get_type(node);
  if (type && type->kind != PSX_TYPE_POINTER) return 0;
  return node && node->kind == ND_DEREF &&
         node->type_state.is_scalar_ptr_member_lvalue;
}

int ps_node_scalar_ptr_member_lvalue(node_t *node) {
  return node_scalar_ptr_member_lvalue(node);
}

int ps_node_subscript_deref_uses_base_address(node_t *node) {
  if (!node || node->kind != ND_DEREF) return 0;
  psx_type_t *type = ps_node_get_type(node);
  if (type && type->kind == PSX_TYPE_ARRAY) return 1;
  return node->type_state.subscript_uses_base_address;
}

int ps_node_deref_decays_to_address(node_t *node) {
  if (!node || node->kind != ND_DEREF) return 0;
  psx_type_t *type = ps_node_get_type(node);
  return type && type->kind == PSX_TYPE_ARRAY;
}

psx_type_t *ps_node_row_decay_pointer_arith_type(node_t *node) {
  if (!node || (node->kind != ND_DEREF && node->kind != ND_ADDR)) return NULL;
  int ds = ps_node_deref_size(node);
  if (ds <= 0 || ps_node_type_size(node) <= ds) return NULL;

  psx_type_t *type = ps_node_get_type(node);
  psx_type_t *base = (type && type->kind == PSX_TYPE_ARRAY && type->base)
                         ? type->base
                         : NULL;
  if (!base) return NULL;

  psx_type_t *ptr = ps_type_new_pointer(base, ds);
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

int ps_node_bitfield_width(node_t *node) {
  return node ? node->type_state.bit_width : 0;
}

int ps_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed) {
  if (node && node->type_state.bit_width > 0) {
    if (bit_width) *bit_width = node->type_state.bit_width;
    if (bit_offset) *bit_offset = node->type_state.bit_offset;
    if (bit_is_signed) *bit_is_signed = node->type_state.bit_is_signed;
    return 1;
  }
  return 0;
}

int ps_node_value_is_pointer_like(node_t *node) {
  if (!node) return 0;
  if (ps_type_is_pointer(ps_node_get_type(node))) return 1;
  if (ps_node_scalar_ptr_member_lvalue(node)) return 1;
  return 0;
}

int ps_node_aggregate_value_size(node_t *node) {
  if (!node) return 0;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_pointer = 0;
  ps_node_get_tag_type(node, &tag_kind, &tag_name, &tag_len, &is_tag_pointer);
  if (is_tag_pointer || !ps_ctx_is_tag_aggregate_kind(tag_kind)) return 0;
  if (ps_node_value_is_pointer_like(node)) return 0;
  int size = ps_node_type_size(node);
  if (size <= 0) size = ps_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  return size > 0 ? size : 0;
}

int ps_node_vla_alloc_descriptor_info(node_t *node, int *descriptor_frame_off,
                                       int *row_stride_frame_off) {
  if (descriptor_frame_off) *descriptor_frame_off = 0;
  if (row_stride_frame_off) *row_stride_frame_off = 0;
  if (!node || node->kind != ND_VLA_ALLOC) return 0;
  node_vla_alloc_t *alloc = (node_vla_alloc_t *)node;
  if (descriptor_frame_off) *descriptor_frame_off = alloc->descriptor_frame_off;
  if (row_stride_frame_off) *row_stride_frame_off = alloc->row_stride_frame_off;
  return alloc->descriptor_frame_off > 0;
}

node_t *ps_node_new_vla_alloc(int descriptor_frame_off,
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

node_t *psx_node_new_raw_assign(node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_ASSIGN;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

node_t *ps_node_new_assign(node_t *lhs, node_t *rhs) {
  node_t *node = psx_node_new_raw_assign(lhs, rhs);
  ps_node_bind_type(node, ps_node_get_type(lhs));
  return node;
}

node_t *psx_node_new_raw_decl_initializer(node_t *target, node_t *value,
                                          psx_decl_init_kind_t init_kind,
                                          token_t *tok) {
  node_decl_init_t *node = arena_alloc(sizeof(node_decl_init_t));
  node->base.kind = ND_DECL_INIT;
  node->base.lhs = target;
  node->base.rhs = value;
  node->base.tok = tok;
  node->init_kind = init_kind;
  return (node_t *)node;
}

node_t *psx_node_new_compound_literal(
    psx_type_name_ref_t type_name, node_t *initializer, token_t *tok,
    int requires_addressable_object, int has_file_scope_storage) {
  node_compound_literal_t *node =
      arena_alloc(sizeof(node_compound_literal_t));
  node->base.kind = ND_COMPOUND_LITERAL;
  node->base.rhs = initializer;
  node->base.tok = tok;
  node->type_name = type_name;
  node->requires_addressable_object =
      requires_addressable_object ? 1 : 0;
  node->has_file_scope_storage = has_file_scope_storage ? 1 : 0;
  return (node_t *)node;
}

node_t *psx_node_new_raw_decl_initializer_list(
    node_t *target, psx_decl_init_kind_t init_kind,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok) {
  return psx_node_new_raw_decl_initializer(
      target, psx_node_new_initializer_list(entries, entry_count, tok),
      init_kind, tok);
}

node_t *psx_node_new_initializer_list(
    psx_initializer_entry_t *entries, int entry_count, token_t *tok) {
  node_init_list_t *node = arena_alloc(sizeof(node_init_list_t));
  node->base.kind = ND_INIT_LIST;
  node->base.tok = tok;
  node->entries = entries;
  node->entry_count = entry_count;
  return (node_t *)node;
}

void ps_node_reject_const_assign_at(node_t *node, const char *op,
                                     token_t *tok) {
  (void)op;
  if (!node) return;
  if (node->kind == ND_GENERIC_SELECTION) {
    node_generic_selection_t *selection =
        (node_generic_selection_t *)node;
    int selected = selection->selected_index >= 0
                       ? selection->selected_index
                       : ps_node_generic_selection_index(selection);
    if (selected >= 0 && selected < selection->association_count) {
      ps_node_reject_const_assign_at(
          selection->associations[selected].expression, op, tok);
    }
    return;
  }
  if (node->kind == ND_LVAR || node->kind == ND_GVAR ||
      node->kind == ND_MEMBER_ACCESS ||
      node->kind == ND_UNARY_DEREF || node->kind == ND_DEREF) {
    /* ag_c の慣習: ポインタ変数の is_const_qualified は「pointee の const」を
     * 表す (_Generic の判定等で利用)。「変数自身の const」は
     * pointer_const_qual_mask の bit 0 で保持される。
     * したがって p = q を拒否するのはこのビットが立っているときのみ
     * (`int * const p;` のケース)。非ポインタ変数は従来通り
     * is_const_qualified を見る (`const int x = 5; x = 10;` を拒否)。 */
    if (node_self_is_const_qualified(node)) {
      diag_emit_tokf(DIAG_ERR_PARSER_CONST_ASSIGNMENT, tok,
                     diag_message_for(DIAG_ERR_PARSER_CONST_ASSIGNMENT));
    }
  }
}

void psx_node_reject_const_assign(node_t *node, const char *op) {
  ps_node_reject_const_assign_at(node, op, curtok());
}

static int node_pointee_is_const(node_t *node) {
  if (!node) return 0;
  return ps_node_pointee_is_const_qualified(node);
}

void ps_node_reject_const_qual_discard_at(node_t *lhs, node_t *rhs,
                                           token_t *tok) {
  if (!lhs || !rhs) return;
  if (lhs->kind != ND_LVAR && lhs->kind != ND_GVAR) return;
  if (!ps_node_is_pointer(lhs)) return;
  if (psx_node_has_funcptr_signature(lhs) &&
      psx_node_has_funcptr_signature(rhs)) {
    return;
  }
  if (ps_node_pointee_is_const_qualified(lhs)) return;
  if (node_pointee_is_const(rhs)) {
    diag_emit_tokf(DIAG_ERR_PARSER_CONST_QUAL_DISCARD, tok,
                   diag_message_for(DIAG_ERR_PARSER_CONST_QUAL_DISCARD));
  }
}

void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs) {
  ps_node_reject_const_qual_discard_at(lhs, rhs, curtok());
}

void ps_node_expect_lvalue_at(node_t *node, const char *op, token_t *tok) {
  if (node && node->kind == ND_GENERIC_SELECTION) {
    node_generic_selection_t *selection =
        (node_generic_selection_t *)node;
    int selected = selection->selected_index >= 0
                       ? selection->selected_index
                       : ps_node_generic_selection_index(selection);
    if (selected >= 0 && selected < selection->association_count) {
      ps_node_expect_lvalue_at(
          selection->associations[selected].expression, op, tok);
      return;
    }
  }
  if (!node || (node->kind != ND_LVAR &&
                node->kind != ND_MEMBER_ACCESS &&
                node->kind != ND_UNARY_DEREF &&
                node->kind != ND_SUBSCRIPT &&
                node->kind != ND_DEREF && node->kind != ND_GVAR)) {
    diag_emit_tokf(DIAG_ERR_PARSER_LVALUE_REQUIRED, tok,
                   diag_message_for(DIAG_ERR_PARSER_LVALUE_REQUIRED), (char *)op);
  }
}

void psx_node_expect_lvalue(node_t *node, const char *op) {
  ps_node_expect_lvalue_at(node, op, curtok());
}

int ps_node_compound_literal_array_size(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_COMMA)
    return ps_node_compound_literal_array_size(node->rhs);
  if (node->kind != ND_ADDR || node->is_explicit_addr_expr || !node->lhs)
    return 0;
  psx_type_t *object_type = ps_node_get_type(node->lhs);
  return object_type && object_type->kind == PSX_TYPE_ARRAY
             ? ps_type_sizeof(object_type)
             : 0;
}
