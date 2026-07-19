#include "node_utils.h"
#include "../semantic/resolution_state.h"
#include "../semantic/resolved_node.h"
#include "../semantic/resolved_node_kind.h"
#include "../semantic/resolved_object_ref.h"
#include "lvar_internal.h"
#include "decl.h"
#include "semantic_ctx.h"
#include "type_builder.h"
#include "arena.h"
#include "diag.h"
#include "../diag/diag.h"
#include "../semantic/initializer_resolution.h"
#include "../semantic/record_decl_table.h"
#include "../semantic/vla_runtime_plan.h"
#include "../type_layout.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int type_is_pointer_view_type(const psx_type_t *type);
static const psx_type_t *lvar_decl_type_view(const lvar_t *var);
static const psx_type_t *gvar_decl_type_view(const global_var_t *gv);

typedef enum {
  NODE_SCALAR_UNSIGNED,
  NODE_SCALAR_LONG_LONG,
  NODE_SCALAR_PLAIN_CHAR,
  NODE_SCALAR_LONG_DOUBLE,
} node_scalar_flag_t;

static int node_self_is_const_qualified(
    const psx_resolution_store_t *store, node_t *node);
static int node_self_is_volatile_qualified(
    const psx_resolution_store_t *store, node_t *node);
static void gvar_tag_identity(const global_var_t *gv, token_kind_t *kind,
                              char **name, int *len, int *scope_depth_p1);
static int node_scalar_ptr_member_lvalue(
    const psx_resolution_store_t *store, node_t *node);

static void *resolution_node_alloc_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, size_t size) {
  return psx_resolution_node_alloc_in(store, arena_context, size);
}

static int tag_scope_depth_from_p1(int scope_depth_p1) {
  return scope_depth_p1 > 0 ? scope_depth_p1 - 1 : -1;
}

static int ctx_get_tag_member_count_scoped(
    psx_semantic_context_t *semantic_context,
    token_kind_t tk, char *tn, int tl, int scope_depth_p1) {
  int scope_depth = tag_scope_depth_from_p1(scope_depth_p1);
  if (scope_depth >= 0) {
    int n = ps_ctx_get_tag_member_count_at_scope_in(
        semantic_context, tk, tn, tl, scope_depth);
    if (n >= 0) return n;
  }
  return ps_ctx_get_tag_member_count_in(semantic_context, tk, tn, tl);
}

static int ctx_get_tag_member_scoped(
    psx_semantic_context_t *semantic_context,
    token_kind_t tk, char *tn, int tl, int scope_depth_p1, int idx,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  int scope_depth = tag_scope_depth_from_p1(scope_depth_p1);
  if (scope_depth >= 0) {
    return ps_ctx_get_tag_member_at_scope_in(
        semantic_context, tk, tn, tl, scope_depth, idx,
        out_declaration, out_layout);
  }
  return ps_ctx_get_tag_member_in(
      semantic_context, tk, tn, tl, idx,
      out_declaration, out_layout);
}

static psx_type_t *type_with_self_qualifiers_in(
    arena_context_t *arena_context, const psx_type_t *type,
    int is_const_qualified, int is_volatile_qualified) {
  if (!type) return NULL;
  psx_type_t *copy = arena_alloc_in(arena_context, sizeof(psx_type_t));
  *copy = *type;
  if (copy->kind == PSX_TYPE_ARRAY && copy->base) {
    copy->base = type_with_self_qualifiers_in(
        arena_context, copy->base,
        is_const_qualified, is_volatile_qualified);
    return copy;
  }
  psx_type_qualifiers_t qualifiers = PSX_TYPE_QUALIFIER_NONE;
  if (is_const_qualified) qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  if (is_volatile_qualified) qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
  ps_type_add_qualifiers(copy, qualifiers);
  return copy;
}

static const psx_type_t *lvar_decl_type_consistent(const lvar_t *var);
static const psx_type_t *gvar_decl_type_consistent(const global_var_t *gv);

static const psx_type_t *lvar_decl_type_view(const lvar_t *var) {
  return var ? lvar_decl_type_consistent(var) : NULL;
}

static const psx_type_t *gvar_decl_type_view(const global_var_t *gv) {
  return gv ? gvar_decl_type_consistent(gv) : NULL;
}

static token_kind_t type_tag_aggregate_kind(const psx_type_t *type) {
  if (!type) return TK_EOF;
  type = ps_type_array_leaf_type(type);
  if (!type) return TK_EOF;
  if (type->kind == PSX_TYPE_STRUCT) return TK_STRUCT;
  if (type->kind == PSX_TYPE_UNION) return TK_UNION;
  return TK_EOF;
}

static const psx_type_t *lvar_decl_type_consistent(const lvar_t *var) {
  if (!var) return NULL;
  return psx_semantic_type_table_lookup_qual_type(
      var->decl_type_table, var->decl_qual_type);
}

static const psx_type_t *gvar_decl_type_consistent(const global_var_t *gv) {
  if (!gv) return NULL;
  return psx_semantic_type_table_lookup_qual_type(
      gv->decl_type_table, gv->decl_qual_type);
}

int ps_lvar_value_is_pointer_like(const lvar_t *var) {
  const psx_type_t *type = lvar_decl_type_view(var);
  return type ? ps_type_is_pointer_like(type) : 0;
}

int ps_lvar_is_struct_aggregate(const lvar_t *var) {
  const psx_type_t *type = lvar_decl_type_view(var);
  return type ? type_tag_aggregate_kind(type) == TK_STRUCT : 0;
}

int ps_lvar_is_union_aggregate(const lvar_t *var) {
  const psx_type_t *type = lvar_decl_type_view(var);
  return type ? type_tag_aggregate_kind(type) == TK_UNION : 0;
}

int ps_lvar_is_tag_aggregate(const lvar_t *var) {
  return ps_lvar_is_struct_aggregate(var) || ps_lvar_is_union_aggregate(var);
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

int ps_gvar_is_array(const global_var_t *gv) {
  const psx_type_t *type = gvar_decl_type_view(gv);
  return type && type->kind == PSX_TYPE_ARRAY ? 1 : 0;
}

int ps_gvar_is_struct_aggregate(const global_var_t *gv) {
  const psx_type_t *type = gvar_decl_type_view(gv);
  return type ? type_tag_aggregate_kind(type) == TK_STRUCT : 0;
}

int ps_gvar_is_union_aggregate(const global_var_t *gv) {
  const psx_type_t *type = gvar_decl_type_view(gv);
  return type ? type_tag_aggregate_kind(type) == TK_UNION : 0;
}

int ps_gvar_is_tag_aggregate(const global_var_t *gv) {
  return ps_gvar_is_struct_aggregate(gv) || ps_gvar_is_union_aggregate(gv);
}

static tk_float_kind_t gvar_initializer_fp_kind(const global_var_t *gv) {
  const psx_type_t *type =
      ps_type_array_leaf_type(gvar_decl_type_view(gv));
  if (!type || (type->kind != PSX_TYPE_FLOAT &&
                type->kind != PSX_TYPE_COMPLEX))
    return TK_FLOAT_KIND_NONE;
  return ps_type_floating_token_kind(type);
}

int ps_gvar_is_bool_scalar(const global_var_t *gv) {
  const psx_type_t *type = gvar_decl_type_view(gv);
  return type && type->kind == PSX_TYPE_BOOL ? 1 : 0;
}

int ps_gvar_array_element_is_bool(const global_var_t *gv) {
  const psx_type_t *type = gvar_decl_type_view(gv);
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  const psx_type_t *leaf = ps_type_array_leaf_type(type);
  return leaf && leaf->kind == PSX_TYPE_BOOL ? 1 : 0;
}

psx_gvar_initializer_class_t
ps_gvar_initializer_class(const global_var_t *gv, int include_empty_aggregate) {
  int is_tag_aggregate = ps_gvar_is_tag_aggregate(gv);
  psx_gvar_initializer_class_t cls = {
      .kind = PSX_GVAR_INIT_KIND_INTEGER,
      .is_tag_aggregate = is_tag_aggregate,
      .has_aggregate_initializer =
          is_tag_aggregate && gv && gv->init_count > 0,
      .has_explicit_initializer = gv && gv->has_init,
      .has_payload = 0,
  };
  if (is_tag_aggregate) {
    cls.has_payload = cls.has_aggregate_initializer;
    if (include_empty_aggregate || cls.has_aggregate_initializer) {
      cls.kind = PSX_GVAR_INIT_KIND_AGGREGATE;
    }
    return cls;
  }
  if (gv && gv->init_symbol) {
    cls.kind = PSX_GVAR_INIT_KIND_SYMBOL;
    cls.has_payload = 1;
    return cls;
  }
  if (gv && gv->init_count > 0) {
    cls.kind = PSX_GVAR_INIT_KIND_SLOTS;
    cls.has_payload = 1;
    return cls;
  }
  if (gvar_initializer_fp_kind(gv) != TK_FLOAT_KIND_NONE) {
    cls.kind = PSX_GVAR_INIT_KIND_FLOAT;
    cls.has_payload = 1;
    return cls;
  }
  cls.has_payload = gv && gv->has_init;
  return cls;
}

int ps_gvar_has_aggregate_initializer(const global_var_t *gv) {
  return ps_gvar_initializer_class(gv, 0).has_aggregate_initializer;
}

int ps_gvar_has_explicit_initializer(const global_var_t *gv) {
  return ps_gvar_initializer_class(gv, 0).has_explicit_initializer;
}

static psx_gvar_init_slots_layout_t gvar_init_slots_layout(
    const global_var_t *gv, int element_size, int element_count) {
  tk_float_kind_t fp_kind = gvar_initializer_fp_kind(gv);
  psx_gvar_init_slots_layout_t layout = {
      .elem_size = element_size > 0 ? element_size : 0,
      .elem_count = element_count > 0 ? element_count : 0,
      .init_count = gv ? gv->init_count : 0,
      .is_fp_array = gv && gv->init_fvalues &&
                     (fp_kind == TK_FLOAT_KIND_FLOAT ||
                      fp_kind >= TK_FLOAT_KIND_DOUBLE),
      .fp_kind = fp_kind,
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
  return gvar_make_symbol_ref(gv->init_symbol, gv->init_symbol_len,
                              gv->init_symbol_offset);
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

int ps_gvar_symbol_ref_named_function_in(
    psx_semantic_context_t *semantic_context,
    psx_gvar_symbol_ref_t ref, char **out_name, int *out_len) {
  char *name = NULL;
  int len = 0;
  if (!ps_gvar_symbol_ref_named(ref, &name, &len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (!ps_ctx_has_function_name_in(semantic_context, name, len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (out_name) *out_name = name;
  if (out_len) *out_len = len;
  return 1;
}

psx_gvar_init_member_value_t
ps_gvar_init_member_value(const global_var_t *gv, int idx,
                           const psx_record_member_decl_t *member,
                           int member_size) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  tk_float_kind_t member_fp_kind = psx_record_member_decl_fp_kind(member);
  psx_gvar_init_member_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_init_slot_symbol_ref(&slot),
      .value = slot.value,
      .fvalue = slot.fvalue,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = member_size > 0 ? member_size : 0,
  };
  if (psx_record_member_decl_is_bool(member)) value.value = value.value != 0;
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
ps_gvar_init_scalar_value(const global_var_t *gv, int storage_size) {
  int has_init = gv && gv->has_init;
  tk_float_kind_t fp_kind = gvar_initializer_fp_kind(gv);
  psx_gvar_init_scalar_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_initializer_symbol_ref(gv),
      .value = has_init ? gv->init_val : 0,
      .fvalue = has_init ? gv->fval : 0.0,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = storage_size > 0 ? storage_size : 0,
  };
  if (value.symbol_ref.kind != PSX_GVAR_SYMBOL_REF_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_SYMBOL;
    return value;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    value.kind = PSX_GVAR_INIT_VALUE_FLOAT;
    value.fp_kind = fp_kind;
  }
  return value;
}

int ps_gvar_visit_initializer_classified(
    const global_var_t *gv, const psx_gvar_initializer_class_t *init_class,
    int scalar_size, int slot_element_size, int slot_element_count,
    const psx_gvar_initializer_visit_ops_t *ops, void *user) {
  if (!init_class || !ops) return 0;
  if (init_class->kind == PSX_GVAR_INIT_KIND_AGGREGATE) {
    return ops->aggregate ? ops->aggregate(user, init_class) : 0;
  }
  if (init_class->kind == PSX_GVAR_INIT_KIND_SLOTS) {
    psx_gvar_init_slots_layout_t layout =
        gvar_init_slots_layout(gv, slot_element_size, slot_element_count);
    return ops->slots ? ops->slots(user, &layout, init_class) : 0;
  }
  psx_gvar_init_scalar_value_t value =
      ps_gvar_init_scalar_value(gv, scalar_size);
  return ops->scalar ? ops->scalar(user, value, init_class) : 0;
}

int ps_gvar_array_element_count(const global_var_t *gv) {
  const psx_type_t *type = gvar_decl_type_view(gv);
  if (type && type->kind == PSX_TYPE_ARRAY && type->array_len > 0)
    return type->array_len;
  return 0;
}

static int gvar_tag_identity_from_type(const global_var_t *gv, token_kind_t *kind,
                                       char **name, int *len,
                                       int *scope_depth_p1) {
  const psx_type_t *type =
      ps_type_array_leaf_type(gvar_decl_type_view(gv));
  if (!type || !ps_type_is_tag_aggregate(type)) return 0;
  if (kind) *kind = ps_type_tag_token_kind(type);
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
  psx_type_id_t aggregate_type_id;
  const psx_record_decl_t *record_decl;
} gvar_aggregate_layout_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_target_info_t *target;
  psx_type_id_t aggregate_type_id;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  int ordinal;
  int count;
  const psx_record_decl_t *record_decl;
  psx_tag_flat_cover_state_t cover_state;
} gvar_aggregate_member_iter_t;

static gvar_aggregate_layout_t gvar_aggregate_layout(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t root_type_id,
    const global_var_t *gv);
static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl);
static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      psx_record_member_decl_t *out_declaration,
                                      psx_record_member_layout_t *out_layout,
                                      int *out_ordinal);
static void gvar_aggregate_member_iter_set_next(gvar_aggregate_member_iter_t *iter,
                                                int next_ordinal);
static int gvar_walk_struct_initializer(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int struct_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user);
static int gvar_walk_union_initializer(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int union_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user);
static gvar_init_cursor_t gvar_init_cursor(const global_var_t *gv);
static int gvar_init_cursor_has(const gvar_init_cursor_t *cur);
static int gvar_init_cursor_index(const gvar_init_cursor_t *cur);
static int gvar_init_cursor_advance(gvar_init_cursor_t *cur);
static int gvar_init_cursor_consume_plain_zero_padding(gvar_init_cursor_t *cur,
                                                       int start_idx, int target_slots);
static int gvar_init_cursor_consume_resolved_type_zero_padding(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    gvar_init_cursor_t *cur, int start_idx);
static int gvar_init_cursor_pack_bitfield_unit(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const psx_record_decl_t *record_decl,
    int member_index, gvar_init_cursor_t *cur,
    psx_gvar_bitfield_unit_t *out);
static int tag_union_init_member_for_slot_scoped(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl,
    const global_var_t *gv, int idx,
    psx_record_member_decl_t *out_declaration,
    int *out_ordinal);
static void gvar_aggregate_member_iter_note_cover(
    gvar_aggregate_member_iter_t *iter,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout,
    int member_ordinal);

static int gvar_member_value_size_for_target(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t value_type_id, const ag_target_info_t *target) {
  return ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, value_type_id, target);
}

static int gvar_member_storage_size_for_target(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t member_type_id, const ag_target_info_t *target) {
  return ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, member_type_id, target);
}

static int gvar_get_record_member_layout(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    int member_ordinal, psx_record_member_layout_t *out_layout) {
  const psx_type_t *aggregate_type = psx_semantic_type_table_lookup(
      semantic_types, aggregate_type_id);
  if (!aggregate_type ||
      aggregate_type->record_id == PSX_RECORD_ID_INVALID || !out_layout)
    return 0;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, aggregate_type->record_id, target);
  const psx_record_member_layout_t *member_layout =
      psx_record_layout_member(layout, member_ordinal);
  if (!member_layout) return 0;
  *out_layout = *member_layout;
  return 1;
}

static gvar_aggregate_layout_t gvar_aggregate_layout(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t root_type_id,
    const global_var_t *gv) {
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int tag_scope_depth_p1 = 0;
  gvar_tag_identity(gv, &tag_kind, &tag_name, &tag_len, &tag_scope_depth_p1);
  const psx_type_t *decl_type = psx_semantic_type_table_lookup(
      semantic_types, root_type_id);
  int type_size = ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, root_type_id, target);
  psx_type_id_t aggregate_type_id =
      psx_semantic_type_table_array_leaf(
          semantic_types, root_type_id).type_id;
  const psx_type_t *aggregate_type = psx_semantic_type_table_lookup(
      semantic_types, aggregate_type_id);
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
      .aggregate_type_id = aggregate_type_id,
      .record_decl =
          aggregate_type && ps_type_is_tag_aggregate(aggregate_type)
              ? psx_record_decl_table_lookup(
                    record_decls, ps_type_record_id(aggregate_type))
              : NULL,
  };
  if (layout.is_array) {
    layout.elem_size = ps_type_sizeof_id_with_records(
        semantic_types, record_layouts, aggregate_type_id, target);
    layout.elem_count = ps_type_array_flat_element_count(decl_type);
  }
  return layout;
}

static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl) {
  gvar_aggregate_member_iter_t iter = {
      .semantic_context = semantic_context,
      .semantic_types = semantic_types,
      .record_decls = record_decls,
      .record_layouts = record_layouts,
      .target = target,
      .aggregate_type_id = aggregate_type_id,
      .tag_kind = tag_kind,
      .tag_name = tag_name,
      .tag_len = tag_len,
      .tag_scope_depth_p1 = tag_scope_depth_p1,
      .ordinal = 0,
      .count = record_decl
                   ? record_decl->member_count
                   : ctx_get_tag_member_count_scoped(
                         semantic_context, tag_kind, tag_name, tag_len,
                         tag_scope_depth_p1),
      .record_decl = record_decl,
  };
  ps_tag_flat_cover_state_init(&iter.cover_state);
  return iter;
}

static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      psx_record_member_decl_t *out_declaration,
                                      psx_record_member_layout_t *out_layout,
                                      int *out_ordinal) {
  if (!iter || !out_declaration || !out_layout) return 0;
  while (iter->ordinal < iter->count) {
    int ordinal = iter->ordinal++;
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (iter->record_decl) {
      declaration = iter->record_decl->members[ordinal];
    } else if (!ctx_get_tag_member_scoped(
                   iter->semantic_context,
                   iter->tag_kind, iter->tag_name, iter->tag_len,
                   iter->tag_scope_depth_p1, ordinal,
                   &declaration, &layout)) {
        return 0;
      }
    if (ps_record_member_decl_is_unnamed_struct(&declaration)) continue;
    if (!gvar_get_record_member_layout(
            iter->semantic_types, iter->record_layouts, iter->target,
            iter->aggregate_type_id, ordinal, &layout))
      return 0;
    if (ps_tag_flat_cover_state_covers(
            &iter->cover_state, layout.offset))
      continue;
    gvar_aggregate_member_iter_note_cover(
        iter, &declaration, &layout, ordinal);
    *out_declaration = declaration;
    *out_layout = layout;
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

static void gvar_walk_emit_scalar(
    const psx_gvar_aggregate_walk_ops_t *ops, void *user,
    const psx_record_member_decl_t *declaration,
    psx_type_id_t value_type_id,
    int slot, long long offset) {
  ops->scalar(user, declaration, value_type_id, slot, offset);
}

static void gvar_walk_emit_bitfield_member(
    const psx_gvar_aggregate_walk_ops_t *ops, void *user,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout,
    psx_type_id_t value_type_id,
    int slot, long long offset) {
  ops->bitfield_member(
      user, declaration, layout, value_type_id, slot, offset);
}

static void gvar_walk_emit_padding(const psx_gvar_aggregate_walk_ops_t *ops,
                                   void *user, long long offset, int size) {
  if (ops && ops->padding && size > 0) ops->padding(user, offset, size);
}

static int gvar_walk_needs_padding(const psx_gvar_aggregate_walk_ops_t *ops) {
  return ops && ops->padding;
}

static const psx_record_decl_t *gvar_member_declaration_record_decl(
    const psx_record_decl_table_t *record_decls,
    const psx_record_member_decl_t *member) {
  const psx_type_t *type = member
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(member))
                               : NULL;
  return type && ps_type_is_tag_aggregate(type)
             ? psx_record_decl_table_lookup(
                   record_decls, ps_type_record_id(type))
             : NULL;
}

static int gvar_resolved_record_find_unnamed_union_covering_offset(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    int base_offset, int target_offset,
    int *out_offset, int *out_size) {
  const psx_type_t *aggregate_type = psx_semantic_type_table_lookup(
      semantic_types, aggregate_type_id);
  if (!aggregate_type || !record_decl ||
      aggregate_type->record_id == PSX_RECORD_ID_INVALID)
    return 0;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, aggregate_type->record_id, target);
  if (!layout) return 0;
  int member_count = record_decl->member_count;
  if (layout->member_count < member_count) member_count = layout->member_count;
  for (int i = 0; i < member_count; i++) {
    const psx_record_member_decl_t *member = &record_decl->members[i];
    psx_record_member_layout_t member_layout = {0};
    if (!ps_record_member_decl_is_unnamed_aggregate(member) ||
        !gvar_get_record_member_layout(
            semantic_types, record_layouts, target, aggregate_type_id,
            i, &member_layout))
      continue;
    psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
        semantic_types, aggregate_type_id, i).type_id;
    int member_size = ps_type_sizeof_id_with_records(
        semantic_types, record_layouts, member_type_id, target);
    int start = base_offset + member_layout.offset;
    if (member_size <= 0 || target_offset < start ||
        target_offset >= start + member_size)
      continue;
    if (ps_record_member_decl_is_unnamed_union(member)) {
      if (out_offset) *out_offset = start;
      if (out_size) *out_size = member_size;
      return 1;
    }
    psx_type_id_t child_type_id = psx_semantic_type_table_array_leaf(
        semantic_types, member_type_id).type_id;
    if (gvar_resolved_record_find_unnamed_union_covering_offset(
            semantic_types, record_decls, record_layouts, target,
            child_type_id,
            gvar_member_declaration_record_decl(record_decls, member), start,
            target_offset, out_offset, out_size))
      return 1;
  }
  return 0;
}

static void gvar_aggregate_member_iter_note_cover(
    gvar_aggregate_member_iter_t *iter,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout,
    int member_ordinal) {
  if (!iter || !declaration || !layout) return;
  if (!iter->record_decl) {
    ps_tag_flat_cover_state_note_in(
        iter->semantic_context, &iter->cover_state, iter->tag_kind,
        iter->tag_name, iter->tag_len, declaration, layout);
    return;
  }
  psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
      iter->semantic_types, iter->aggregate_type_id,
      member_ordinal).type_id;
  if (ps_record_member_decl_is_unnamed_union(declaration)) {
    int member_size = ps_type_sizeof_id_with_records(
        iter->semantic_types, iter->record_layouts, member_type_id,
        iter->target);
    if (member_size > 0) {
      iter->cover_state.covered_union_off = layout->offset;
      iter->cover_state.covered_union_size = member_size;
    }
    return;
  }
  int cover_offset = 0;
  int cover_size = 0;
  if (gvar_resolved_record_find_unnamed_union_covering_offset(
          iter->semantic_types, iter->record_decls,
          iter->record_layouts, iter->target,
          iter->aggregate_type_id, iter->record_decl, 0, layout->offset,
          &cover_offset, &cover_size)) {
    iter->cover_state.covered_union_off = cover_offset;
    iter->cover_state.covered_union_size = cover_size;
  }
}

static psx_type_id_t gvar_member_type_id(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t aggregate_type_id, int member_ordinal) {
  return psx_semantic_type_table_record_member(
      semantic_types, aggregate_type_id, member_ordinal).type_id;
}

static psx_type_id_t gvar_member_value_type_id(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t aggregate_type_id, int member_ordinal) {
  psx_type_id_t member_type_id = gvar_member_type_id(
      semantic_types, aggregate_type_id, member_ordinal);
  const psx_type_t *member_type = psx_semantic_type_table_lookup(
      semantic_types, member_type_id);
  return member_type && member_type->kind == PSX_TYPE_ARRAY
             ? psx_semantic_type_table_array_leaf(
                   semantic_types, member_type_id).type_id
             : member_type_id;
}

static const psx_type_t *record_member_decl_tag_type(
    const psx_record_member_decl_t *declaration) {
  const psx_type_t *type = psx_record_member_decl_type(declaration);
  while (type &&
         (type->kind == PSX_TYPE_ARRAY || type->kind == PSX_TYPE_POINTER)) {
    type = type->base;
  }
  return ps_type_is_tag_aggregate(type) ? type : NULL;
}

static int gvar_walk_struct_initializer(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int struct_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!cur) return 1;
  int prev_end = 0;
  gvar_aggregate_member_iter_t iter =
      gvar_aggregate_member_iter(
          semantic_context, semantic_types, record_decls,
          record_layouts, target,
          aggregate_type_id, tag_kind, tag_name, tag_len,
          tag_scope_depth_p1, record_decl);
  while (gvar_init_cursor_has(cur)) {
    psx_record_member_decl_t member = {0};
    psx_record_member_layout_t member_layout = {0};
    int ordinal = 0;
    if (!gvar_aggregate_member_next(
            &iter, &member, &member_layout, &ordinal))
      break;
    psx_type_id_t member_type_id = gvar_member_type_id(
        semantic_types, aggregate_type_id, ordinal);
    psx_type_id_t member_value_type_id = gvar_member_value_type_id(
        semantic_types, aggregate_type_id, ordinal);
    psx_type_id_t member_aggregate_type_id =
        psx_semantic_type_table_array_leaf(
            semantic_types, member_type_id).type_id;
    if (member_layout.offset > prev_end) {
      gvar_walk_emit_padding(
          ops, user, base_offset + prev_end,
          member_layout.offset - prev_end);
    }
    if (member.bit_width > 0) {
      if (!gvar_walk_require_bitfield_unit(ops)) return 0;
      psx_gvar_bitfield_unit_t unit = {0};
      if (!gvar_init_cursor_pack_bitfield_unit(
              semantic_context, semantic_types, record_layouts,
              target, aggregate_type_id,
              tag_kind, tag_name, tag_len, record_decl,
              ordinal, cur, &unit)) {
        return 0;
      }
      ops->bitfield_unit(user, &unit, base_offset);
      gvar_aggregate_member_iter_set_next(&iter, unit.last_member_index + 1);
      prev_end = unit.offset + unit.size;
      continue;
    }
    int member_value_size =
        gvar_member_value_size_for_target(
            semantic_types, record_layouts, member_value_type_id, target);
    int member_storage_size =
        gvar_member_storage_size_for_target(
            semantic_types, record_layouts, member_type_id, target);
    int member_array_count =
        ps_type_array_flat_element_count(member.decl_type);
    const psx_type_t *member_tag_type =
        record_member_decl_tag_type(&member);
    token_kind_t member_tag_kind = member_tag_type
                                       ? ps_type_tag_token_kind(member_tag_type)
                                       : TK_EOF;
    char *member_tag_name = member_tag_type
                                ? member_tag_type->tag_name : NULL;
    int member_tag_len = member_tag_type ? member_tag_type->tag_len : 0;
    const psx_record_decl_t *member_record =
        gvar_member_declaration_record_decl(record_decls, &member);
    if (member_array_count > 0) {
      if (ps_record_member_decl_is_tag_aggregate(&member)) {
        for (int k = 0; k < member_array_count; k++) {
          if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
          int elem_start_idx = gvar_init_cursor_index(cur);
          long long elem_off = base_offset + member_layout.offset +
                               (long long)k * member_value_size;
          int ok = ps_record_member_decl_is_union_aggregate(&member)
              ? gvar_walk_union_initializer(semantic_context,
                                            semantic_types,
                                            record_decls,
                                            record_layouts,
                                            target,
                                            member_aggregate_type_id,
                                            member_tag_kind, member_tag_name, member_tag_len,
                                            0, member_record,
                                            gv, cur, elem_off, member_value_size,
                                            ops, user)
              : gvar_walk_struct_initializer(semantic_context,
                                             semantic_types,
                                             record_decls,
                                             record_layouts,
                                             target,
                                             member_aggregate_type_id,
                                             member_tag_kind, member_tag_name, member_tag_len,
                                             0, member_record,
                                             gv, cur, elem_off, member_value_size,
                                             ops, user);
          if (!ok) return 0;
          gvar_init_cursor_consume_resolved_type_zero_padding(
              semantic_types, record_decls, record_layouts, target,
              member_aggregate_type_id, cur, elem_start_idx);
        }
      } else {
        if (!gvar_walk_require_scalar(ops)) return 0;
        for (int k = 0; k < member_array_count; k++) {
          long long elem_off = base_offset + member_layout.offset +
                               (long long)k * member_value_size;
          if (!gvar_init_cursor_has(cur)) {
            if (gvar_walk_needs_padding(ops)) {
              gvar_walk_emit_padding(ops, user, elem_off, member_value_size);
              continue;
            }
            break;
          }
          int slot = gvar_init_cursor_advance(cur);
          gvar_walk_emit_scalar(
              ops, user, &member, member_value_type_id, slot, elem_off);
        }
      }
      prev_end = member_layout.offset + member_storage_size;
      continue;
    }
    if (ps_record_member_decl_is_struct_aggregate(&member)) {
      int member_start_idx = gvar_init_cursor_index(cur);
      if (!gvar_walk_struct_initializer(semantic_context,
                                        semantic_types,
                                        record_decls,
                                        record_layouts,
                                        target,
                                        member_aggregate_type_id,
                                        member_tag_kind, member_tag_name,
                                        member_tag_len, 0, member_record,
                                        gv, cur,
                                        base_offset + member_layout.offset,
                                        member_value_size,
                                        ops, user)) {
        return 0;
      }
      gvar_init_cursor_consume_resolved_type_zero_padding(
          semantic_types, record_decls, record_layouts, target,
          member_aggregate_type_id, cur, member_start_idx);
      prev_end = member_layout.offset + member_value_size;
      continue;
    }
    if (ps_record_member_decl_is_union_aggregate(&member)) {
      if (!gvar_walk_union_initializer(semantic_context,
                                       semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       member_aggregate_type_id,
                                       member_tag_kind, member_tag_name,
                                       member_tag_len, 0, member_record,
                                       gv, cur,
                                       base_offset + member_layout.offset,
                                       member_value_size,
                                       ops, user)) {
        return 0;
      }
      prev_end = member_layout.offset + member_value_size;
      continue;
    }
    if (!gvar_walk_require_scalar(ops)) return 0;
    int slot = gvar_init_cursor_advance(cur);
    gvar_walk_emit_scalar(
        ops, user, &member, member_value_type_id, slot,
        base_offset + member_layout.offset);
    prev_end = member_layout.offset + member_value_size;
  }
  if (prev_end < struct_size) {
    gvar_walk_emit_padding(ops, user, base_offset + prev_end, struct_size - prev_end);
  }
  return 1;
}

static int gvar_walk_union_initializer(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int union_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!gvar_init_cursor_has(cur)) {
    gvar_walk_emit_padding(ops, user, base_offset, union_size);
    return 1;
  }
  int start_idx = gvar_init_cursor_index(cur);
  psx_record_member_decl_t member = {0};
  psx_record_member_layout_t member_layout = {0};
  int member_ordinal = -1;
  if (!tag_union_init_member_for_slot_scoped(semantic_context,
                                            tag_kind, tag_name, tag_len,
                                            tag_scope_depth_p1, record_decl, gv,
                                            gvar_init_cursor_index(cur), &member,
                                            &member_ordinal)) {
    if (ops && ops->padding) {
      gvar_walk_emit_padding(ops, user, base_offset, union_size);
      return 1;
    }
    return 0;
  }
  if (!gvar_get_record_member_layout(
          semantic_types, record_layouts, target, aggregate_type_id,
          member_ordinal, &member_layout))
    return 0;
  psx_type_id_t member_type_id = gvar_member_type_id(
      semantic_types, aggregate_type_id, member_ordinal);
  psx_type_id_t member_value_type_id = gvar_member_value_type_id(
      semantic_types, aggregate_type_id, member_ordinal);
  psx_type_id_t member_aggregate_type_id =
      psx_semantic_type_table_array_leaf(
          semantic_types, member_type_id).type_id;
  int member_value_size =
      gvar_member_value_size_for_target(
          semantic_types, record_layouts, member_value_type_id, target);
  int member_storage_size =
      gvar_member_storage_size_for_target(
          semantic_types, record_layouts, member_type_id, target);
  int member_array_count =
      ps_type_array_flat_element_count(member.decl_type);
  int emitted = member_array_count > 0 ? member_storage_size : member_value_size;
  const psx_type_t *member_tag_type = record_member_decl_tag_type(&member);
  token_kind_t member_tag_kind = member_tag_type
                                     ? ps_type_tag_token_kind(member_tag_type)
                                     : TK_EOF;
  char *member_tag_name = member_tag_type
                              ? member_tag_type->tag_name : NULL;
  int member_tag_len = member_tag_type ? member_tag_type->tag_len : 0;
  const psx_record_decl_t *member_record =
      gvar_member_declaration_record_decl(record_decls, &member);
  if (member_layout.offset > 0)
    gvar_walk_emit_padding(ops, user, base_offset, member_layout.offset);
  if (member.bit_width > 0) {
    if (!gvar_walk_require_bitfield_member(ops)) return 0;
    int slot = gvar_init_cursor_advance(cur);
    gvar_walk_emit_bitfield_member(
        ops, user, &member, &member_layout, member_value_type_id, slot,
        base_offset + member_layout.offset);
    gvar_init_cursor_consume_resolved_type_zero_padding(
        semantic_types, record_decls, record_layouts, target,
        aggregate_type_id, cur, start_idx);
    if (member_layout.offset + member_value_size < union_size) {
      gvar_walk_emit_padding(
          ops, user, base_offset + member_layout.offset + member_value_size,
          union_size - (member_layout.offset + member_value_size));
    }
    return 1;
  }
  if (member_array_count > 0) {
    if (ps_record_member_decl_is_tag_aggregate(&member)) {
      for (int k = 0; k < member_array_count; k++) {
        if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
        long long elem_off = base_offset + member_layout.offset +
                             (long long)k * member_value_size;
        int ok = ps_record_member_decl_is_struct_aggregate(&member)
            ? gvar_walk_struct_initializer(semantic_context,
                                           semantic_types,
                                           record_decls,
                                           record_layouts,
                                           target,
                                           member_aggregate_type_id,
                                           member_tag_kind, member_tag_name, member_tag_len,
                                           0, member_record,
                                           gv, cur, elem_off, member_value_size,
                                           ops, user)
            : gvar_walk_union_initializer(semantic_context,
                                          semantic_types,
                                          record_decls,
                                          record_layouts,
                                          target,
                                          member_aggregate_type_id,
                                          member_tag_kind, member_tag_name, member_tag_len,
                                          0, member_record,
                                          gv, cur, elem_off, member_value_size,
                                          ops, user);
        if (!ok) return 0;
      }
    } else {
      if (!gvar_walk_require_scalar(ops)) return 0;
      for (int k = 0; k < member_array_count; k++) {
        long long elem_off = base_offset + member_layout.offset +
                             (long long)k * member_value_size;
        if (!gvar_init_cursor_has(cur)) {
          if (gvar_walk_needs_padding(ops)) {
            gvar_walk_emit_padding(ops, user, elem_off, member_value_size);
            continue;
          }
          break;
        }
        int slot = gvar_init_cursor_advance(cur);
        gvar_walk_emit_scalar(
            ops, user, &member, member_value_type_id, slot, elem_off);
      }
    }
    if (member_layout.offset + emitted < union_size) {
      gvar_walk_emit_padding(
          ops, user, base_offset + member_layout.offset + emitted,
          union_size - (member_layout.offset + emitted));
    }
    return 1;
  }
  if (ps_record_member_decl_is_tag_aggregate(&member)) {
    int ok = ps_record_member_decl_is_struct_aggregate(&member)
        ? gvar_walk_struct_initializer(semantic_context,
                                       semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       member_aggregate_type_id,
                                       member_tag_kind, member_tag_name, member_tag_len,
                                       0, member_record,
                                       gv, cur, base_offset + member_layout.offset,
                                       member_value_size, ops, user)
        : gvar_walk_union_initializer(semantic_context,
                                      semantic_types,
                                      record_decls,
                                      record_layouts,
                                      target,
                                      member_aggregate_type_id,
                                      member_tag_kind, member_tag_name, member_tag_len,
                                      0, member_record,
                                      gv, cur, base_offset + member_layout.offset,
                                      member_value_size, ops, user);
    if (!ok) return 0;
    gvar_init_cursor_consume_resolved_type_zero_padding(
        semantic_types, record_decls, record_layouts, target,
        aggregate_type_id, cur, start_idx);
    if (member_layout.offset + emitted < union_size) {
      gvar_walk_emit_padding(
          ops, user, base_offset + member_layout.offset + emitted,
          union_size - (member_layout.offset + emitted));
    }
    return 1;
  }
  if (!gvar_walk_require_scalar(ops)) return 0;
  int slot = gvar_init_cursor_advance(cur);
  gvar_walk_emit_scalar(
      ops, user, &member, member_value_type_id, slot,
      base_offset + member_layout.offset);
  gvar_init_cursor_consume_resolved_type_zero_padding(
      semantic_types, record_decls, record_layouts, target,
      aggregate_type_id, cur, start_idx);
  if (member_layout.offset + member_value_size < union_size) {
    gvar_walk_emit_padding(
        ops, user, base_offset + member_layout.offset + member_value_size,
        union_size - (member_layout.offset + member_value_size));
  }
  return 1;
}

static int gvar_walk_aggregate_initializer(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t root_type_id,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user,
    int require_resolved_record_decl) {
  if (!ps_gvar_is_tag_aggregate(gv)) return 0;
  gvar_aggregate_layout_t layout =
      gvar_aggregate_layout(
          semantic_types, record_decls, record_layouts, target,
          root_type_id, gv);
  if (require_resolved_record_decl && !layout.record_decl) return 0;
  gvar_init_cursor_t cur = gvar_init_cursor(gv);
  if (!layout.is_array) {
    return layout.is_union
        ? gvar_walk_union_initializer(semantic_context,
                                      semantic_types,
                                      record_decls,
                                      record_layouts,
                                      target,
                                      layout.aggregate_type_id,
                                      layout.tag_kind, layout.tag_name,
                                      layout.tag_len, layout.tag_scope_depth_p1,
                                      layout.record_decl,
                                      gv, &cur, base_offset, layout.type_size,
                                      ops, user)
        : gvar_walk_struct_initializer(semantic_context,
                                       semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       layout.aggregate_type_id,
                                       layout.tag_kind, layout.tag_name,
                                       layout.tag_len, layout.tag_scope_depth_p1,
                                       layout.record_decl,
                                       gv, &cur, base_offset, layout.type_size,
                                       ops, user);
  }
  for (int e = 0; e < layout.elem_count; e++) {
    if (!gvar_init_cursor_has(&cur) && !gvar_walk_needs_padding(ops)) break;
    long long elem_off = base_offset + (long long)e * layout.elem_size;
    if (layout.is_union) {
      if (!gvar_walk_union_initializer(semantic_context,
                                       semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       layout.aggregate_type_id,
                                       layout.tag_kind, layout.tag_name,
                                       layout.tag_len, layout.tag_scope_depth_p1,
                                       layout.record_decl,
                                       gv, &cur, elem_off, layout.elem_size,
                                       ops, user)) {
        return 0;
      }
    } else {
      int elem_start_idx = gvar_init_cursor_index(&cur);
      if (!gvar_walk_struct_initializer(semantic_context,
                                        semantic_types,
                                        record_decls,
                                        record_layouts,
                                        target,
                                        layout.aggregate_type_id,
                                        layout.tag_kind, layout.tag_name,
                                        layout.tag_len, layout.tag_scope_depth_p1,
                                        layout.record_decl,
                                        gv, &cur, elem_off, layout.elem_size,
                                        ops, user)) {
        return 0;
      }
      gvar_init_cursor_consume_resolved_type_zero_padding(
          semantic_types, record_decls, record_layouts, target,
          layout.aggregate_type_id, &cur, elem_start_idx);
    }
  }
  return 1;
}

int ps_gvar_walk_aggregate_initializer_in(
    psx_semantic_context_t *semantic_context,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!semantic_context) return 0;
  return gvar_walk_aggregate_initializer(
      semantic_context,
      ps_ctx_semantic_type_table_in(semantic_context),
      ps_ctx_record_decl_table_in(semantic_context),
      ps_ctx_record_layout_table_in(semantic_context),
      ps_ctx_target_info(semantic_context), ps_gvar_decl_type_id(gv),
      gv, base_offset, ops, user, 0);
}

int ps_gvar_walk_resolved_aggregate_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t root_type_id,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!semantic_types || !record_decls || !record_layouts || !target ||
      root_type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return gvar_walk_aggregate_initializer(
      NULL, semantic_types, record_decls, record_layouts, target, root_type_id,
      gv, base_offset, ops, user, 1);
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
  return (gvar_init_cursor_t){
      .gv = gv,
      .index = 0,
      .count = gv ? gv->init_count : 0,
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

static int record_member_declaration_storage_size_in(
    psx_semantic_context_t *semantic_context,
    const psx_record_member_decl_t *member) {
  return ps_ctx_type_sizeof_in(semantic_context,
                               psx_record_member_decl_type(member));
}

static int gvar_init_cursor_consume_resolved_type_zero_padding(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    gvar_init_cursor_t *cur, int start_idx) {
  return gvar_init_cursor_consume_plain_zero_padding(
      cur, start_idx,
      psx_initializer_flat_slot_count_with_records(
          semantic_types, record_decls, record_layouts, target,
          aggregate_type_id));
}

unsigned long long ps_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  unsigned long long mask = bit_width >= 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
  return ((unsigned long long)slot.value & mask) << bit_offset;
}

static int gvar_init_cursor_pack_bitfield_unit(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const psx_record_decl_t *record_decl,
    int member_index, gvar_init_cursor_t *cur,
    psx_gvar_bitfield_unit_t *out) {
  if (!cur || !out) return 0;
  psx_record_member_decl_t first = {0};
  psx_record_member_layout_t first_layout = {0};
  int n_members = record_decl
                      ? record_decl->member_count
                      : ps_ctx_get_tag_member_count_in(
                            semantic_context, tag_kind, tag_name, tag_len);
  if (record_decl && member_index >= 0 && member_index < n_members) {
    first = record_decl->members[member_index];
  } else if (!ps_ctx_get_tag_member_in(
                 semantic_context, tag_kind, tag_name, tag_len,
                 member_index, &first, &first_layout)) {
    return 0;
  }
  if (!gvar_get_record_member_layout(
          semantic_types, record_layouts, target, aggregate_type_id,
          member_index, &first_layout))
    return 0;
  if (first.bit_width <= 0) return 0;
  int unit_off = first_layout.offset;
  psx_type_id_t unit_type_id = gvar_member_value_type_id(
      semantic_types, aggregate_type_id, member_index);
  int unit_size = ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, unit_type_id, target);
  if (unit_size <= 0) return 0;
  unsigned long long packed = 0;
  int m = member_index;
  int last = member_index;
  while (m < n_members && gvar_init_cursor_has(cur)) {
    psx_record_member_decl_t member = {0};
    psx_record_member_layout_t member_layout = {0};
    if (record_decl) {
      member = record_decl->members[m];
    } else if (!ps_ctx_get_tag_member_in(
                   semantic_context, tag_kind, tag_name, tag_len, m,
                   &member, &member_layout)) {
      break;
    }
    if (!gvar_get_record_member_layout(
            semantic_types, record_layouts, target, aggregate_type_id,
            m, &member_layout))
      break;
    if (member.bit_width <= 0 || member_layout.offset != unit_off) break;
    packed |= ps_gvar_init_slot_bitfield_bits(cur->gv, cur->index,
                                               member.bit_width,
                                               member_layout.bit_offset);
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

static int record_member_decl_fp_size(
    const psx_record_member_decl_t *declaration) {
  const psx_type_t *type = declaration
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(declaration))
                               : NULL;
  tk_float_kind_t fp_kind =
      type && (type->kind == PSX_TYPE_FLOAT ||
               type->kind == PSX_TYPE_COMPLEX)
          ? ps_type_floating_token_kind(type)
          : TK_FLOAT_KIND_NONE;
  return fp_kind == TK_FLOAT_KIND_FLOAT ? 4
       : fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 0;
}

static const psx_type_t *record_member_decl_direct_tag_leaf(
    const psx_record_member_decl_t *declaration) {
  const psx_type_t *type = psx_record_member_decl_type(declaration);
  if (!type || type->kind == PSX_TYPE_POINTER) return NULL;
  type = ps_type_array_leaf_type(type);
  return ps_type_is_tag_aggregate(type) ? type : NULL;
}

int ps_record_member_decl_is_struct_aggregate(
    const psx_record_member_decl_t *declaration) {
  const psx_type_t *leaf =
      record_member_decl_direct_tag_leaf(declaration);
  return leaf && leaf->kind == PSX_TYPE_STRUCT;
}

int ps_record_member_decl_is_union_aggregate(
    const psx_record_member_decl_t *declaration) {
  const psx_type_t *leaf =
      record_member_decl_direct_tag_leaf(declaration);
  return leaf && leaf->kind == PSX_TYPE_UNION;
}

int ps_record_member_decl_is_tag_aggregate(
    const psx_record_member_decl_t *declaration) {
  return ps_record_member_decl_is_struct_aggregate(declaration) ||
         ps_record_member_decl_is_union_aggregate(declaration);
}

int ps_record_member_decl_is_unnamed_struct(
    const psx_record_member_decl_t *declaration) {
  return declaration && declaration->len == 0 &&
         ps_record_member_decl_is_struct_aggregate(declaration);
}

int ps_record_member_decl_is_unnamed_union(
    const psx_record_member_decl_t *declaration) {
  return declaration && declaration->len == 0 &&
         ps_record_member_decl_is_union_aggregate(declaration);
}

int ps_record_member_decl_is_unnamed_aggregate(
    const psx_record_member_decl_t *declaration) {
  return ps_record_member_decl_is_unnamed_struct(declaration) ||
         ps_record_member_decl_is_unnamed_union(declaration);
}

void ps_tag_flat_cover_state_init(psx_tag_flat_cover_state_t *state) {
  if (!state) return;
  state->covered_union_off = 0;
  state->covered_union_size = 0;
}

int ps_tag_flat_cover_state_covers(const psx_tag_flat_cover_state_t *state,
                                    int member_offset) {
  if (!state || state->covered_union_size <= 0) return 0;
  return member_offset >= state->covered_union_off &&
         member_offset < state->covered_union_off + state->covered_union_size;
}

int ps_tag_find_unnamed_union_covering_offset_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int base_off, int target_off, int *out_off, int *out_size);

void ps_tag_flat_cover_state_note_in(
    psx_semantic_context_t *semantic_context,
    psx_tag_flat_cover_state_t *state,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const psx_record_member_decl_t *declaration,
    const psx_record_member_layout_t *layout) {
  if (!state || !declaration || !layout) return;
  if (ps_record_member_decl_is_unnamed_union(declaration)) {
    state->covered_union_off = layout->offset;
    state->covered_union_size =
        record_member_declaration_storage_size_in(
            semantic_context, declaration);
    return;
  }
  int cover_off = 0;
  int cover_size = 0;
  if (ps_tag_find_unnamed_union_covering_offset_in(
          semantic_context, tag_kind, tag_name, tag_len,
          0, layout->offset, &cover_off, &cover_size)) {
    state->covered_union_off = cover_off;
    state->covered_union_size = cover_size;
  }
}

int ps_tag_find_unnamed_union_covering_offset_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int base_off, int target_off, int *out_off, int *out_size) {
  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, i,
            &declaration, &layout))
      break;
    if (!ps_record_member_decl_is_unnamed_aggregate(&declaration)) continue;
    int start = base_off + layout.offset;
    int member_storage_size = record_member_declaration_storage_size_in(
        semantic_context, &declaration);
    int end = start + member_storage_size;
    if (target_off < start || target_off >= end) continue;
    if (ps_record_member_decl_is_union_aggregate(&declaration)) {
      if (out_off) *out_off = start;
      if (out_size) *out_size = member_storage_size;
      return 1;
    }
    if (ps_record_member_decl_is_struct_aggregate(&declaration)) {
      const psx_type_t *child_type =
          record_member_decl_direct_tag_leaf(&declaration);
      token_kind_t child_kind = child_type
                                    ? ps_type_tag_token_kind(child_type)
                                    : TK_EOF;
      char *child_name = child_type ? child_type->tag_name : NULL;
      int child_len = child_type ? child_type->tag_len : 0;
      if (ps_tag_find_unnamed_union_covering_offset_in(
              semantic_context, child_kind, child_name, child_len,
              start, target_off, out_off, out_size)) {
        return 1;
      }
    }
  }
  return 0;
}

int ps_record_member_decl_flat_slots_in(
    psx_semantic_context_t *semantic_context,
    const psx_record_member_decl_t *declaration) {
  if (ps_record_member_decl_is_unnamed_struct(declaration)) return 0;
  int per = 1;
  if (ps_record_member_decl_is_tag_aggregate(declaration)) {
    const psx_type_t *leaf =
        record_member_decl_direct_tag_leaf(declaration);
    per = ps_tag_flat_slot_count_in(
        semantic_context, ps_type_tag_token_kind(leaf),
        leaf->tag_name, leaf->tag_len);
  }
  int count = ps_type_array_flat_element_count(
      psx_record_member_decl_type(declaration));
  return count > 0 ? count * per : per;
}

int ps_record_member_decl_elem_flat_slots_in(
    psx_semantic_context_t *semantic_context,
    const psx_record_member_decl_t *declaration) {
  if (!declaration) return 1;
  int total = ps_record_member_decl_flat_slots_in(
      semantic_context, declaration);
  int count = ps_type_array_flat_element_count(
      psx_record_member_decl_type(declaration));
  if (count > 0) {
    int per = total / count;
    return per > 0 ? per : 1;
  }
  return total > 0 ? total : 1;
}

int ps_record_member_decl_subscript_stride_slots_in(
    psx_semantic_context_t *semantic_context,
    const psx_record_member_decl_t *declaration) {
  int per = ps_record_member_decl_elem_flat_slots_in(
      semantic_context, declaration);
  const psx_type_t *type = psx_record_member_decl_type(declaration);
  if (!type || type->kind != PSX_TYPE_ARRAY) return per;
  int stride = ps_type_array_flat_element_count(type->base);
  if (stride > 0) per *= stride;
  return per > 0 ? per : 1;
}

int ps_tag_flat_slot_count_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len) {
  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  int slots = 0;
  int union_max_bytes = 0;
  psx_tag_flat_cover_state_t cover_state;
  ps_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n; i++) {
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, i,
            &declaration, &layout))
      break;
    if (tag_kind == TK_UNION) {
      int ms = ps_record_member_decl_flat_slots_in(
          semantic_context, &declaration);
      int bytes = record_member_declaration_storage_size_in(
          semantic_context, &declaration);
      if (bytes > union_max_bytes || (bytes == union_max_bytes && ms > slots)) {
        union_max_bytes = bytes;
        slots = ms;
      }
      continue;
    }
    if (ps_record_member_decl_is_unnamed_struct(&declaration)) continue;
    if (ps_tag_flat_cover_state_covers(&cover_state, layout.offset)) continue;
    slots += ps_record_member_decl_flat_slots_in(
        semantic_context, &declaration);
    ps_tag_flat_cover_state_note_in(
        semantic_context, &cover_state, tag_kind, tag_name, tag_len,
        &declaration, &layout);
  }
  return slots > 0 ? slots : 1;
}

int ps_tag_member_at_flat_slot_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int flat_slot, psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout, int *out_ordinal) {
  if (flat_slot < 0) return 0;
  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  int slot = 0;
  psx_tag_flat_cover_state_t cover_state;
  ps_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n; i++) {
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, i,
            &declaration, &layout))
      break;
    if (ps_record_member_decl_is_unnamed_struct(&declaration)) continue;
    if (ps_tag_flat_cover_state_covers(&cover_state, layout.offset)) continue;
    int member_slots = ps_record_member_decl_flat_slots_in(
        semantic_context, &declaration);
    if (flat_slot < slot + member_slots) {
      if (out_declaration) *out_declaration = declaration;
      if (out_layout) *out_layout = layout;
      if (out_ordinal) *out_ordinal = i;
      return 1;
    }
    ps_tag_flat_cover_state_note_in(
        semantic_context, &cover_state, tag_kind, tag_name, tag_len,
        &declaration, &layout);
    slot += member_slots;
  }
  return 0;
}

int ps_tag_next_named_member_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *ordinal_inout, psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  if (!ordinal_inout) return 0;
  int ordinal = *ordinal_inout;
  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  while (ordinal < n) {
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, ordinal,
            &declaration, &layout)) {
      *ordinal_inout = ordinal + 1;
      return 0;
    }
    ordinal++;
    if (declaration.len <= 0) continue;
    if (out_declaration) *out_declaration = declaration;
    if (out_layout) *out_layout = layout;
    *ordinal_inout = ordinal;
    return 1;
  }
  *ordinal_inout = ordinal;
  return 0;
}

int ps_tag_first_named_member_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout, int *out_ordinal) {
  int ordinal = 0;
  if (!ps_tag_next_named_member_in(
          semantic_context, tag_kind, tag_name, tag_len, &ordinal,
          out_declaration, out_layout))
    return 0;
  if (out_ordinal) *out_ordinal = ordinal - 1;
  return 1;
}

int ps_tag_find_named_member_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    char *member_name, int member_len,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout, int *out_ordinal) {
  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, i,
            &declaration, &layout))
      break;
    if (declaration.len == member_len && declaration.name &&
        strncmp(declaration.name, member_name, (size_t)member_len) == 0) {
      if (out_declaration) *out_declaration = declaration;
      if (out_layout) *out_layout = layout;
      if (out_ordinal) *out_ordinal = i;
      return 1;
    }
  }
  return 0;
}

int ps_tag_select_union_member_for_init_slot_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const global_var_t *gv, int idx,
    psx_record_member_decl_t *declaration,
    psx_record_member_layout_t *layout) {
  if (!declaration || !layout) return 0;
  int init_fp_size = ps_gvar_union_init_slot_fp_size(gv, idx);
  int selected_fp_size = record_member_decl_fp_size(declaration);
  if (init_fp_size == selected_fp_size) return 0;
  if (init_fp_size == 0 && selected_fp_size == 0) return 0;

  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  for (int i = 0; i < n; i++) {
    psx_record_member_decl_t candidate = {0};
    psx_record_member_layout_t candidate_layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, i,
            &candidate, &candidate_layout))
      break;
    int cand_fp_size = record_member_decl_fp_size(&candidate);
    if ((init_fp_size > 0 && cand_fp_size == init_fp_size) ||
        (init_fp_size == 0 && cand_fp_size == 0)) {
      *declaration = candidate;
      *layout = candidate_layout;
      return 1;
    }
  }
  return 0;
}

int ps_tag_union_init_member_for_slot_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const global_var_t *gv, int idx,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  int ordinal = ps_gvar_union_init_slot_ordinal(gv, idx);
  psx_record_member_decl_t declaration = {0};
  psx_record_member_layout_t layout = {0};
  if (ordinal < 0 ||
      !ps_ctx_get_tag_member_in(
          semantic_context, tag_kind, tag_name, tag_len, ordinal,
          &declaration, &layout))
    return 0;
  ps_tag_select_union_member_for_init_slot_in(
      semantic_context, tag_kind, tag_name, tag_len, gv, idx,
      &declaration, &layout);
  if (out_declaration) *out_declaration = declaration;
  if (out_layout) *out_layout = layout;
  return 1;
}

static int tag_union_init_member_for_slot_scoped(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1,
    const psx_record_decl_t *record_decl,
    const global_var_t *gv, int idx,
    psx_record_member_decl_t *out_declaration,
    int *out_ordinal) {
  if (!out_declaration) return 0;
  int ordinal = ps_gvar_union_init_slot_ordinal(gv, idx);
  if (record_decl) {
    if (ordinal < 0 || ordinal >= record_decl->member_count) return 0;
    *out_declaration = record_decl->members[ordinal];
    int init_fp_size = ps_gvar_union_init_slot_fp_size(gv, idx);
    int selected_fp_size = record_member_decl_fp_size(out_declaration);
    if (init_fp_size != selected_fp_size &&
        !(init_fp_size == 0 && selected_fp_size == 0)) {
      for (int i = 0; i < record_decl->member_count; i++) {
        psx_record_member_decl_t candidate = record_decl->members[i];
        int candidate_fp_size = record_member_decl_fp_size(&candidate);
        if ((init_fp_size > 0 && candidate_fp_size == init_fp_size) ||
            (init_fp_size == 0 && candidate_fp_size == 0)) {
          *out_declaration = candidate;
          ordinal = i;
          break;
        }
      }
    }
  } else {
    psx_record_member_layout_t ignored_layout = {0};
    if (!ctx_get_tag_member_scoped(
            semantic_context, tag_kind, tag_name, tag_len,
            tag_scope_depth_p1, ordinal, out_declaration, &ignored_layout))
      return 0;
    int init_fp_size = ps_gvar_union_init_slot_fp_size(gv, idx);
    int selected_fp_size = record_member_decl_fp_size(out_declaration);
    if (init_fp_size != selected_fp_size &&
        !(init_fp_size == 0 && selected_fp_size == 0)) {
      int count = ctx_get_tag_member_count_scoped(
          semantic_context, tag_kind, tag_name, tag_len,
          tag_scope_depth_p1);
      for (int i = 0; i < count; i++) {
        psx_record_member_decl_t candidate = {0};
        psx_record_member_layout_t candidate_layout = {0};
        if (!ctx_get_tag_member_scoped(
                semantic_context, tag_kind, tag_name, tag_len,
                tag_scope_depth_p1, i, &candidate, &candidate_layout))
          break;
        int candidate_fp_size = record_member_decl_fp_size(&candidate);
        if ((init_fp_size > 0 && candidate_fp_size == init_fp_size) ||
            (init_fp_size == 0 && candidate_fp_size == 0)) {
          *out_declaration = candidate;
          ordinal = i;
          break;
        }
      }
    }
  }
  if (out_ordinal) *out_ordinal = ordinal;
  return 1;
}

int ps_tag_member_designator_slot_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    char *member_name, int member_len, int *out_ordinal) {
  int n = ps_ctx_get_tag_member_count_in(
      semantic_context, tag_kind, tag_name, tag_len);
  int slot = 0;
  int covered_union_slot = -1;
  int covered_union_off = 0;
  int covered_union_size = 0;
  for (int i = 0; i < n; i++) {
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    if (!ps_ctx_get_tag_member_in(
            semantic_context, tag_kind, tag_name, tag_len, i,
            &declaration, &layout))
      break;
    int in_covered_union = covered_union_slot >= 0 &&
                           layout.offset >= covered_union_off &&
                           layout.offset < covered_union_off +
                                               covered_union_size;
    if (declaration.len == member_len && declaration.name &&
        strncmp(declaration.name, member_name, (size_t)member_len) == 0) {
      if (out_ordinal) *out_ordinal = i;
      if (in_covered_union) return covered_union_slot;
      return tag_kind == TK_UNION ? 0 : slot;
    }
    if (ps_record_member_decl_is_unnamed_struct(&declaration)) continue;
    if (ps_record_member_decl_is_unnamed_union(&declaration)) {
      covered_union_slot = slot;
      covered_union_off = layout.offset;
      covered_union_size = record_member_declaration_storage_size_in(
          semantic_context, &declaration);
      slot += ps_record_member_decl_flat_slots_in(
          semantic_context, &declaration);
      continue;
    }
    if (in_covered_union) continue;
    int cover_off = 0;
    int cover_size = 0;
    int has_cover = ps_tag_find_unnamed_union_covering_offset_in(
        semantic_context, tag_kind, tag_name, tag_len,
        0, layout.offset, &cover_off, &cover_size);
    if (has_cover) {
      covered_union_slot = slot;
      covered_union_off = cover_off;
      covered_union_size = cover_size;
    }
    slot += ps_record_member_decl_flat_slots_in(
        semantic_context, &declaration);
  }
  return -1;
}

const psx_type_t *ps_gvar_get_decl_type(const global_var_t *gv) {
  return gvar_decl_type_consistent(gv);
}

psx_qual_type_t ps_gvar_decl_qual_type(const global_var_t *gv) {
  return gv ? gv->decl_qual_type
            : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                PSX_TYPE_QUALIFIER_NONE};
}

psx_type_id_t ps_gvar_decl_type_id(const global_var_t *gv) {
  return ps_gvar_decl_qual_type(gv).type_id;
}

static int type_is_integer_like(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER;
}

static int type_result_unsigned(const psx_type_t *type) {
  return type && type->kind != PSX_TYPE_POINTER && ps_type_is_unsigned(type);
}

int ps_node_generic_selection_index(
    const psx_resolution_store_t *store,
    node_generic_selection_t *selection) {
  if (!selection || !selection->control ||
      selection->association_count <= 0) {
    return -1;
  }
  int count = selection->association_count;
  const psx_type_t **types =
      calloc((size_t)count, sizeof(*types));
  unsigned char *defaults = calloc((size_t)count, sizeof(*defaults));
  if (!types || !defaults) {
    free(types);
    free(defaults);
    return -1;
  }
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, &selection->base);
  const psx_generic_selection_resolution_state_t *resolution =
      state ? &state->generic_selection : NULL;
  for (int i = 0; i < count; i++) {
    types[i] = resolution && resolution->association_type_names &&
                       i < resolution->association_type_name_count
                   ? resolution->association_type_names[i].resolved_type
                   : NULL;
    defaults[i] = selection->associations[i].is_default;
  }
  int selected = ps_type_generic_select_index(
      ps_node_get_type(store, selection->control), types, defaults, count);
  free(types);
  free(defaults);
  return selected;
}

static node_t *generic_selection_semantic_expression(
    const psx_resolution_store_t *store,
    node_generic_selection_t *selection) {
  if (!selection) return NULL;
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, &selection->base);
  const psx_generic_selection_resolution_state_t *resolution =
      state ? &state->generic_selection : NULL;
  int selected = resolution && resolution->is_resolved
                     ? resolution->selected_index
                     : ps_node_generic_selection_index(store, selection);
  return selected >= 0 && selected < selection->association_count
             ? selection->associations[selected].expression : NULL;
}

static int node_type_accepts_vla_runtime_view(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_type_t *type = ps_node_get_type(store, node);
  return type && ps_type_is_well_formed(type) &&
         ps_type_contains_vla_array(type);
}

static node_t *bound_node_vla_runtime_source(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_ADD:
      if (node->lhs &&
          ps_node_vla_row_stride_frame_off(store, node->lhs) != 0)
        return node->lhs;
      return node->rhs;
    case ND_SUB:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_CAST:
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return node->lhs;
    case ND_COMMA:
    case ND_STMT_EXPR:
      return node->rhs;
    case ND_TERNARY:
      return node->rhs;
    default:
      return NULL;
  }
}

void ps_node_set_vla_runtime_view(
    psx_resolution_store_t *store, node_t *node,
    int row_stride_frame_off, int strides_remaining) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->expr.vla_runtime =
      (psx_vla_runtime_view_t){0};
  if (!node_type_accepts_vla_runtime_view(store, node) ||
      row_stride_frame_off <= 0)
    return;
  state->expr.vla_runtime.row_stride_frame_off =
      row_stride_frame_off;
  state->expr.vla_runtime.strides_remaining =
      strides_remaining > 0 ? strides_remaining : 0;
}

void ps_node_bind_type(
    psx_resolution_store_t *store, node_t *node,
    const psx_type_t *type) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->type = type;
  state->qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!node_type_accepts_vla_runtime_view(store, node)) {
    state->expr.vla_runtime =
        (psx_vla_runtime_view_t){0};
    return;
  }
  node_t *source = bound_node_vla_runtime_source(store, node);
  if (source) {
    int frame_off = ps_node_vla_row_stride_frame_off(store, source);
    int remaining = ps_node_vla_strides_remaining(store, source);
    ps_node_set_vla_runtime_view(store, node, frame_off, remaining);
  } else if (psx_resolution_node_kind(store, node) == ND_DEREF && node->lhs) {
    int frame_off = ps_node_vla_row_stride_frame_off(store, node->lhs);
    int remaining = ps_node_vla_strides_remaining(store, node->lhs);
    ps_node_set_vla_runtime_view(
        store, node, frame_off != 0 && remaining > 0 ? frame_off + 8 : 0,
        remaining > 0 ? remaining - 1 : 0);
  }
}

void ps_node_bind_qual_type(
    psx_resolution_store_t *store, node_t *node,
    const psx_type_t *canonical_type,
    psx_qual_type_t qual_type) {
  if (!node || !canonical_type) return;
  ps_node_bind_type(store, node, canonical_type);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID) return;
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->qual_type = qual_type;
}

void ps_node_clear_type(psx_resolution_store_t *store, node_t *node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->type = NULL;
  state->qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  state->expr.vla_runtime =
      (psx_vla_runtime_view_t){0};
}

void ps_node_clear_expr_type_state(
    psx_resolution_store_t *store, node_t *node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->expr = (psx_expr_type_state_t){0};
}

void ps_node_set_qual_type_identity(
    psx_resolution_store_t *store, node_t *node,
    psx_qual_type_t qual_type) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->qual_type = qual_type;
}

void ps_node_set_bitfield_info(
    psx_resolution_store_t *store, node_t *node,
    int bit_width, int bit_offset, int bit_is_signed) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->expr.bit_width =
      bit_width > 0 ? (unsigned char)bit_width : 0;
  state->expr.bit_offset =
      bit_width > 0 && bit_offset > 0 ? (unsigned char)bit_offset : 0;
  state->expr.bit_is_signed =
      bit_width > 0 && bit_is_signed ? 1 : 0;
}

void ps_node_set_scalar_ptr_member_lvalue(
    psx_resolution_store_t *store, node_t *node, int enabled) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->expr.is_scalar_ptr_member_lvalue =
      enabled ? 1 : 0;
}

void ps_node_set_subscript_uses_base_address(
    psx_resolution_store_t *store, node_t *node, int enabled) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->expr.subscript_uses_base_address =
      enabled ? 1 : 0;
}

static int type_is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

static int scalar_flag_from_type(const psx_type_t *type, node_scalar_flag_t flag) {
  if (!type || type_is_pointer_view_type(type)) return 0;
  switch (flag) {
    case NODE_SCALAR_UNSIGNED:
      return ps_type_is_unsigned(type);
    case NODE_SCALAR_LONG_LONG:
      return type->integer_kind == PSX_INTEGER_KIND_LONG_LONG ? 1 : 0;
    case NODE_SCALAR_PLAIN_CHAR:
      return type->is_plain_char ? 1 : 0;
    case NODE_SCALAR_LONG_DOUBLE:
      return type->floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE ? 1 : 0;
    default:
      return 0;
  }
}

static const psx_type_t *node_pointee_value_type(
    const psx_resolution_store_t *store, node_t *node) {
  return node ? ps_type_pointee_value_type(
                    ps_node_get_type(store, node)) : NULL;
}

static int node_pointee_is_const_qualified(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_type_t *pointee = node_pointee_value_type(store, node);
  return ps_type_has_qualifier(pointee, PSX_TYPE_QUALIFIER_CONST);
}

static int node_pointee_is_volatile_qualified(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_type_t *pointee = node_pointee_value_type(store, node);
  return ps_type_has_qualifier(pointee, PSX_TYPE_QUALIFIER_VOLATILE);
}

static int node_self_is_const_qualified(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return 0;
  const psx_type_t *type = ps_node_get_type(store, node);
  return ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_CONST);
}

static int node_self_is_volatile_qualified(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return 0;
  const psx_type_t *type = ps_node_get_type(store, node);
  return ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_VOLATILE);
}

int ps_node_is_unsigned_type(
    const psx_resolution_store_t *store, node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(store, node), NODE_SCALAR_UNSIGNED)
              : 0;
}

int ps_node_is_long_long_type(
    const psx_resolution_store_t *store, node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(store, node), NODE_SCALAR_LONG_LONG)
              : 0;
}

int ps_node_is_plain_char_type(
    const psx_resolution_store_t *store, node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(store, node), NODE_SCALAR_PLAIN_CHAR)
              : 0;
}

int ps_node_is_long_double_type(
    const psx_resolution_store_t *store, node_t *node) {
  return node ? scalar_flag_from_type(
                    ps_node_get_type(store, node), NODE_SCALAR_LONG_DOUBLE)
              : 0;
}

/* pointer-to-VLA (`int (*p)[m]`) の行ストライドスロット (実行時値) のフレームオフセット。 */
int ps_node_vla_row_stride_frame_off(
    const psx_resolution_store_t *store, node_t *node) {
  psx_vla_runtime_view_t runtime =
      ps_node_vla_runtime_view(store, node);
  return node_type_accepts_vla_runtime_view(store, node)
             ? runtime.row_stride_frame_off : 0;
}

int ps_node_vla_strides_remaining(
    const psx_resolution_store_t *store, node_t *node) {
  psx_vla_runtime_view_t runtime =
      ps_node_vla_runtime_view(store, node);
  return node_type_accepts_vla_runtime_view(store, node) &&
                 runtime.strides_remaining > 0
             ? runtime.strides_remaining : 0;
}

psx_vla_runtime_view_t ps_node_vla_runtime_view(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? state->expr.vla_runtime
               : (psx_vla_runtime_view_t){0};
}

static int node_is_unsigned(
    const psx_resolution_store_t *store, node_t *node) {
  return node ? type_result_unsigned(ps_node_get_type(store, node)) : 0;
}

tk_float_kind_t ps_node_value_fp_kind(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  const psx_type_t *type = ps_node_get_type(store, node);
  if (type && !ps_type_is_pointer(type) &&
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)) {
    tk_float_kind_t kind = ps_type_floating_token_kind(type);
    return kind != TK_FLOAT_KIND_NONE ? kind : TK_FLOAT_KIND_DOUBLE;
  }
  return TK_FLOAT_KIND_NONE;
}

int ps_node_value_is_complex(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return 0;
  const psx_type_t *type = ps_node_get_type(store, node);
  if (type && !ps_type_is_pointer(type)) return type->kind == PSX_TYPE_COMPLEX;
  return 0;
}

int ps_node_value_is_void(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return 0;
  const psx_type_t *type = ps_node_get_type(store, node);
  if (type) return type->kind == PSX_TYPE_VOID;
  return 0;
}

int ps_node_integer_value_is_unsigned(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_type_t *type = ps_node_get_type(store, node);
  return type_is_integer_like(type) && ps_type_is_unsigned(type);
}

int ps_node_conversion_value_is_unsigned(
    const psx_resolution_store_t *store, node_t *node) {
  return node_is_unsigned(store, node);
}

int ps_node_shift_operation_is_unsigned(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node || (node->kind != ND_SHL && node->kind != ND_SHR)) return 0;
  return node_is_unsigned(store, node);
}

node_t *psx_node_new_raw_binary_in(arena_context_t *arena_context,
                                   psx_syntax_node_kind_t kind, node_t *lhs,
                                   node_t *rhs) {
  node_t *node = arena_alloc_in(arena_context, sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

int ps_node_binary_type_op(
    psx_resolution_node_kind_t kind, psx_type_binary_op_t *op) {
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

node_t *ps_node_new_binary_for_target_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    psx_resolution_node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node) return NULL;
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  psx_type_binary_op_t op;
  const psx_type_t *type = ps_node_binary_type_op(kind, &op)
                               ? ps_type_binary_result_for_target_in(
                                     arena_context, target, op,
                                     ps_node_get_type(store, lhs),
                                     ps_node_get_type(store, rhs))
                               : NULL;
  if (type) {
    ps_node_bind_type(store, node, type);
  }
  return node;
}

node_t *ps_node_new_shift_trunc_extend_for_width_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, int left_shift,
    int execution_size, int is_unsigned) {
  const psx_type_t *operand_type = ps_node_get_type(store, operand);
  if (execution_size < 4) execution_size = 4;
  psx_integer_kind_t execution_kind =
      operand_type && operand_type->kind == PSX_TYPE_INTEGER &&
              operand_type->integer_kind == PSX_INTEGER_KIND_LONG_LONG
          ? PSX_INTEGER_KIND_LONG_LONG
      : execution_size >= 8 ? PSX_INTEGER_KIND_LONG
                            : PSX_INTEGER_KIND_INT;
  psx_type_t *execution_type = ps_type_new_integer_kind_in(
      arena_context, execution_kind, is_unsigned ? 1 : 0, 0);
  node_t *shl = resolution_node_alloc_in(
      store, arena_context, sizeof(*shl));
  if (!shl) return NULL;
  shl->kind = ND_SHL;
  shl->lhs = operand;
  shl->rhs = ps_node_new_num_in(
      store, arena_context, left_shift);
  ps_node_bind_type(store, shl, execution_type);
  node_t *shr = resolution_node_alloc_in(
      store, arena_context, sizeof(*shr));
  if (!shr) return NULL;
  shr->kind = ND_SHR;
  shr->lhs = shl;
  shr->rhs = ps_node_new_num_in(
      store, arena_context, left_shift);
  ps_node_bind_type(store, shr, execution_type);
  return shr;
}

node_t *psx_node_new_syntax_int_in(
    arena_context_t *arena_context, long long val, token_t *tok) {
  node_num_t *node = arena_alloc_in(arena_context, sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->base.tok = tok;
  node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  node->val = val;
  return (node_t *)node;
}

node_t *ps_node_new_num_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, long long val) {
  node_num_t *number = resolution_node_alloc_in(
      store, arena_context, sizeof(*number));
  if (!number) return NULL;
  number->base.kind = ND_NUM;
  number->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  number->val = val;
  node_t *node = &number->base;
  ps_node_bind_type(
      store, node, ps_type_new_integer_kind_in(
                arena_context, PSX_INTEGER_KIND_INT, 0, 0));
  return node;
}

static node_t *annotate_explicit_type(
                                      psx_resolution_store_t *store,
                                      node_t *node,
                                      const psx_type_t *type) {
  if (node && type) ps_node_bind_type(store, node, type);
  return node;
}

node_t *ps_node_new_fp_to_int_cast_in(
                                       psx_resolution_store_t *store,
                                       arena_context_t *arena_context,
                                       node_t *operand,
                                       const psx_type_t *cast_type) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  if (!node ||
      !psx_resolution_node_set_kind(store, node, ND_FP_TO_INT))
    return NULL;
  node->lhs = operand;
  return annotate_explicit_type(store, node, cast_type);
}

node_t *ps_node_new_int_to_fp_cast_in(
                                       psx_resolution_store_t *store,
                                       arena_context_t *arena_context,
                                       node_t *operand,
                                       const psx_type_t *cast_type) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  if (!node ||
      !psx_resolution_node_set_kind(store, node, ND_INT_TO_FP))
    return NULL;
  node->lhs = operand;
  return annotate_explicit_type(store, node, cast_type);
}

node_t *ps_node_new_semantic_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand,
    const psx_type_t *cast_type) {
  node_t *wrap = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  return annotate_explicit_type(store, wrap, cast_type);
}

node_t *ps_node_new_integer_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand,
    const psx_type_t *cast_type) {
  return ps_node_new_integer_cast_result_ex_in(
      store, arena_context, operand, cast_type, 0);
}

node_t *ps_node_new_integer_cast_result_ex_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand,
    const psx_type_t *cast_type, int widen_zext_i64) {
  node_t *wrap = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  wrap->kind = ND_CAST;
  wrap->lhs = operand;
  ps_node_set_widen_zext_i64(store, wrap, widen_zext_i64);
  return annotate_explicit_type(store, wrap, cast_type);
}

node_t *ps_node_new_i64_to_i32_trunc_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand,
    const psx_type_t *cast_type) {
  int is_unsigned = ps_type_is_unsigned(cast_type);
  node_t *trunc = ps_node_new_shift_trunc_extend_for_width_in(
      store, arena_context, operand, 32, 8, is_unsigned);
  return ps_node_new_integer_cast_result_in(
      store, arena_context, trunc, cast_type);
}

node_t *ps_node_new_pointer_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand,
    const psx_type_t *cast_type) {
  return ps_node_new_semantic_cast_result_in(
      store, arena_context, operand, cast_type);
}

node_t *ps_node_new_aggregate_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand,
    const psx_type_t *cast_type) {
  return ps_node_new_semantic_cast_result_in(
      store, arena_context, operand, cast_type);
}

node_t *ps_node_new_void_cast_result_in(
                                        psx_resolution_store_t *store,
                                        arena_context_t *arena_context,
                                        node_t *operand,
                                        const psx_type_t *cast_type) {
  return ps_node_new_semantic_cast_result_in(
      store, arena_context, operand, cast_type);
}

node_t *psx_node_new_source_cast_in(
    arena_context_t *arena_context,
    node_t *operand, psx_type_name_ref_t type_name) {
  node_source_cast_t *cast = arena_alloc_in(
      arena_context, sizeof(node_source_cast_t));
  cast->base.kind = ND_CAST;
  cast->base.lhs = operand;
  cast->base.is_source_cast = 1;
  cast->type_name = type_name;
  return (node_t *)cast;
}

static node_t *new_addr_node(psx_resolution_store_t *store,
                             arena_context_t *arena_context,
                             node_t *base) {
  node_t *addr = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  addr->kind = ND_ADDR;
  addr->lhs = base;
  return addr;
}

node_t *ps_node_new_addr_value_for_in(
                                      psx_resolution_store_t *store,
                                      arena_context_t *arena_context,
                                      node_t *operand) {
  node_t *addr = new_addr_node(store, arena_context, operand);
  ps_node_bind_type(
      store, addr, ps_type_address_result_in(
                arena_context, ps_node_get_type(store, operand)));
  return addr;
}

node_t *ps_node_new_explicit_addr_value_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *operand) {
  if (!operand || operand->kind != ND_ADDR) return operand;
  node_t *cp = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  *cp = *operand;
  ps_node_copy_resolution_state_in(
      store, arena_context, cp, operand);
  ps_node_clear_expr_type_state(store, cp);
  ps_node_bind_type(
      store, cp, ps_type_address_result_in(
              arena_context, ps_node_get_type(store, operand->lhs)));
  cp->is_explicit_addr_expr = 1;
  return cp;
}

node_t *ps_node_new_unary_addr_for_in(
                                      psx_resolution_store_t *store,
                                      arena_context_t *arena_context,
                                      node_t *operand) {
  node_t *node = new_addr_node(store, arena_context, operand);
  ps_node_bind_type(
      store, node, ps_type_address_result_in(
                arena_context, ps_node_get_type(store, operand)));
  node->is_explicit_addr_expr = 1;
  return node;
}

static void init_subscript_expr_state(
    psx_resolution_store_t *store, node_t *result) {
  const psx_type_t *type = ps_node_get_type(store, result);
  if (!type || type->kind != PSX_TYPE_ARRAY) return;
  ps_node_set_subscript_uses_base_address(store, result, 1);
}

static void advance_subscript_vla_runtime_view(
                                                psx_resolution_store_t *store,
                                                node_t *result,
                                                node_t *base) {
  if (!result || !base ||
      !node_type_accepts_vla_runtime_view(store, result)) return;
  int frame_off = ps_node_vla_row_stride_frame_off(store, base);
  int remaining = ps_node_vla_strides_remaining(store, base);
  ps_node_set_vla_runtime_view(
      store, result, frame_off != 0 && remaining > 0 ? frame_off + 8 : 0,
      remaining > 0 ? remaining - 1 : 0);
}

node_t *ps_node_new_tag_member_deref_with_layout_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *addr_base, node_t *base, int member_offset,
    const psx_type_t *member_type, int bit_is_signed,
    int bit_width, int bit_offset) {
  if (!member_type) return NULL;
  node_t *addr = ps_node_new_binary_for_target_in(
      store, arena_context, target, ND_ADD, addr_base,
      ps_node_new_num_in(store, arena_context, member_offset));
  node_t *deref = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  if (!deref ||
      !psx_resolution_node_set_kind(store, deref, ND_DEREF))
    return NULL;
  deref->lhs = addr;
  int mem_array_len =
      ps_type_array_flat_element_count(member_type);
  const psx_type_t *member_value_type = ps_type_array_leaf_type(member_type);
  int mem_is_ptr = member_value_type &&
                   member_value_type->kind == PSX_TYPE_POINTER;
  int member_is_const =
      node_pointee_is_const_qualified(store, base) ||
      (!ps_node_value_is_pointer_like(store, base) &&
       node_self_is_const_qualified(store, base));
  int member_is_volatile =
      node_pointee_is_volatile_qualified(store, base) ||
      (!ps_node_value_is_pointer_like(store, base) &&
       node_self_is_volatile_qualified(store, base));
  ps_node_set_bitfield_info(
      store, deref, bit_width, bit_offset, bit_is_signed);
  ps_node_bind_type(
      store, deref, type_with_self_qualifiers_in(
                 arena_context, member_type,
                 member_is_const, member_is_volatile));
  ps_node_set_scalar_ptr_member_lvalue(
      store, deref, mem_is_ptr && mem_array_len <= 0);
  return deref;
}

node_t *ps_node_new_unary_deref_for_in(
                                       psx_resolution_store_t *store,
                                       arena_context_t *arena_context,
                                       node_t *operand) {
  const psx_type_t *result_type =
      ps_type_dereference_result(ps_node_get_type(store, operand));
  if (!result_type) {
    node_t *result = resolution_node_alloc_in(
        store, arena_context, sizeof(node_t));
    if (!result ||
        !psx_resolution_node_set_kind(store, result, ND_DEREF))
      return NULL;
    result->lhs = operand;
    return result;
  }

  node_t *result = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  if (!result ||
      !psx_resolution_node_set_kind(store, result, ND_DEREF))
    return NULL;
  result->lhs = operand;
  ps_node_bind_type(store, result, result_type);
  return result;
}

node_t *psx_node_new_unary_deref_syntax_for_in(
    arena_context_t *arena_context, node_t *operand) {
  node_t *result = arena_alloc_in(
      arena_context, sizeof(node_t));
  result->kind = ND_UNARY_DEREF;
  result->lhs = operand;
  return result;
}

node_t *psx_node_new_unary_addr_syntax_for_in(
    arena_context_t *arena_context, node_t *operand) {
  node_t *result = arena_alloc_in(
      arena_context, sizeof(node_t));
  result->kind = ND_ADDR;
  result->lhs = operand;
  result->is_explicit_addr_expr = 1;
  return result;
}

node_t *psx_node_new_subscript_syntax_for_in(
    arena_context_t *arena_context, node_t *base, node_t *index) {
  node_t *result = arena_alloc_in(arena_context, sizeof(node_t));
  result->kind = ND_SUBSCRIPT;
  result->lhs = base;
  result->rhs = index;
  return result;
}

node_t *ps_node_new_subscript_deref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *base, node_t *base_addr, node_t *scaled_offset) {
  const psx_type_t *base_type = ps_node_get_type(store, base);
  const psx_type_t *result_type = ps_type_subscript_result_in(
      arena_context, base_type);
  node_t *result = resolution_node_alloc_in(
      store, arena_context, sizeof(node_t));
  if (!result ||
      !psx_resolution_node_set_kind(store, result, ND_DEREF))
    return NULL;
  result->lhs = ps_node_new_binary_for_target_in(
      store, arena_context, target, ND_ADD, base_addr, scaled_offset);
  if (result_type) {
    ps_node_bind_type(store, result, result_type);
    advance_subscript_vla_runtime_view(store, result, base);
    init_subscript_expr_state(store, result);
  }
  return result;
}

static int node_scalar_ptr_member_lvalue(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_type_t *type = ps_node_get_type(store, node);
  if (type && type->kind != PSX_TYPE_POINTER) return 0;
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return node && psx_resolution_node_kind(store, node) == ND_DEREF &&
         state && state->expr.is_scalar_ptr_member_lvalue;
}

int ps_node_scalar_ptr_member_lvalue(
    const psx_resolution_store_t *store, node_t *node) {
  return node_scalar_ptr_member_lvalue(store, node);
}

int ps_node_subscript_deref_uses_base_address(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node || psx_resolution_node_kind(store, node) != ND_DEREF) return 0;
  const psx_type_t *type = ps_node_get_type(store, node);
  if (type && type->kind == PSX_TYPE_ARRAY) return 1;
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state && state->expr.subscript_uses_base_address;
}

int ps_node_deref_decays_to_address(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node || psx_resolution_node_kind(store, node) != ND_DEREF) return 0;
  const psx_type_t *type = ps_node_get_type(store, node);
  return type && type->kind == PSX_TYPE_ARRAY;
}

const psx_type_t *ps_node_array_decay_pointer_arith_type_in(
    const psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node) {
  if (!node ||
      (psx_resolution_node_kind(store, node) != ND_DEREF &&
       node->kind != ND_ADDR))
    return NULL;
  const psx_type_t *type = ps_node_get_type(store, node);
  const psx_type_t *base =
      (type && type->kind == PSX_TYPE_ARRAY && type->base)
          ? type->base
          : NULL;
  if (!base) return NULL;

  return ps_type_address_result_in(arena_context, base);
}

int ps_node_bitfield_width(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? state->expr.bit_width : 0;
}

int ps_node_bitfield_info(
    const psx_resolution_store_t *store, node_t *node,
    int *bit_width, int *bit_offset, int *bit_is_signed) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  if (state && state->expr.bit_width > 0) {
    if (bit_width)
      *bit_width = state->expr.bit_width;
    if (bit_offset)
      *bit_offset = state->expr.bit_offset;
    if (bit_is_signed)
      *bit_is_signed = state->expr.bit_is_signed;
    return 1;
  }
  return 0;
}

int ps_node_value_is_pointer_like(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_ADDR ||
      psx_resolved_object_ref_node_kind(store, node) == ND_FUNCREF)
    return 1;
  if (ps_type_is_pointer_like(ps_node_get_type(store, node))) return 1;
  if (ps_node_scalar_ptr_member_lvalue(store, node)) return 1;
  return 0;
}

int ps_node_vla_alloc_descriptor_info(
    const psx_resolution_store_t *store, node_t *node,
    int *descriptor_frame_off, int *row_stride_frame_off) {
  if (descriptor_frame_off) *descriptor_frame_off = 0;
  if (row_stride_frame_off) *row_stride_frame_off = 0;
  if (!node ||
      psx_resolution_node_kind(store, node) != ND_VLA_ALLOC)
    return 0;
  const psx_vla_runtime_plan_t *plan =
      ((node_vla_alloc_t *)node)->runtime_plan;
  if (!plan) return 0;
  if (descriptor_frame_off)
    *descriptor_frame_off = plan->descriptor_frame_offset;
  if (row_stride_frame_off)
    *row_stride_frame_off = plan->row_stride_frame_offset;
  return plan->descriptor_frame_offset > 0;
}

node_t *ps_node_new_vla_runtime_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    psx_vla_runtime_plan_t *runtime_plan) {
  if (!arena_context || !runtime_plan) return NULL;
  node_vla_alloc_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(node_vla_alloc_t));
  if (!node) return NULL;
  if (!psx_resolution_node_set_kind(store, &node->base, ND_VLA_ALLOC))
    return NULL;
  node->runtime_plan = runtime_plan;
  return (node_t *)node;
}

node_t *psx_node_new_raw_assign_in(arena_context_t *arena_context,
                                   node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc_in(arena_context, sizeof(node_t));
  node->kind = ND_ASSIGN;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

node_t *psx_node_new_static_assert_syntax_in(
    arena_context_t *arena_context, node_t *condition, token_t *token) {
  node_static_assert_t *node = arena_alloc_in(
      arena_context, sizeof(*node));
  if (!node) return NULL;
  node->base.kind = ND_STATIC_ASSERT;
  node->base.tok = token;
  node->condition = condition;
  return &node->base;
}

node_t *ps_node_new_assign_in(psx_resolution_store_t *store,
                              arena_context_t *arena_context,
                              node_t *lhs, node_t *rhs) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node) return NULL;
  node->kind = ND_ASSIGN;
  node->lhs = lhs;
  node->rhs = rhs;
  ps_node_bind_type(store, node, ps_node_get_type(store, lhs));
  return node;
}

node_t *psx_node_new_raw_decl_initializer_in(
    arena_context_t *arena_context, node_t *target, node_t *value,
    psx_decl_init_kind_t init_kind, token_t *tok) {
  node_decl_init_t *node = arena_alloc_in(
      arena_context, sizeof(node_decl_init_t));
  node->base.kind = ND_DECL_INIT;
  node->base.lhs = target;
  node->base.rhs = value;
  node->base.tok = tok;
  node->init_kind = init_kind;
  return (node_t *)node;
}

node_t *psx_node_new_compound_literal_in(
    arena_context_t *arena_context,
    psx_type_name_ref_t type_name, node_t *initializer, token_t *tok,
    int requires_addressable_object, int has_file_scope_storage) {
  node_compound_literal_t *node =
      arena_alloc_in(arena_context, sizeof(node_compound_literal_t));
  node->base.kind = ND_COMPOUND_LITERAL;
  node->base.rhs = initializer;
  node->base.tok = tok;
  node->type_name = type_name;
  node->requires_addressable_object =
      requires_addressable_object ? 1 : 0;
  node->has_file_scope_storage = has_file_scope_storage ? 1 : 0;
  return (node_t *)node;
}

node_t *psx_node_new_raw_decl_initializer_list_in(
    arena_context_t *arena_context,
    node_t *target, psx_decl_init_kind_t init_kind,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok) {
  return psx_node_new_raw_decl_initializer_in(
      arena_context, target,
      psx_node_new_initializer_list_in(
          arena_context, entries, entry_count, tok),
      init_kind, tok);
}

node_t *psx_node_new_initializer_list_in(
    arena_context_t *arena_context,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok) {
  node_init_list_t *node = arena_alloc_in(
      arena_context, sizeof(node_init_list_t));
  node->base.kind = ND_INIT_LIST;
  node->base.tok = tok;
  node->entries = entries;
  node->entry_count = entry_count;
  return (node_t *)node;
}

static psx_qual_type_t node_semantic_qual_type(
    psx_semantic_context_t *semantic_context, node_t *node) {
  if (!node)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_qual_type_t type = ps_node_qual_type(store, node);
  return type.type_id != PSX_TYPE_ID_INVALID
             ? type
             : ps_ctx_intern_qual_type_in(
                   semantic_context, ps_node_get_type(store, node));
}

static int node_semantic_self_has_qualifier(
    psx_semantic_context_t *semantic_context, node_t *node,
    psx_type_qualifiers_t qualifier) {
  return (node_semantic_qual_type(semantic_context, node).qualifiers &
          qualifier) == qualifier;
}

static int node_semantic_pointee_has_qualifier(
    psx_semantic_context_t *semantic_context, node_t *node,
    psx_type_qualifiers_t qualifier) {
  psx_qual_type_t type = node_semantic_qual_type(
      semantic_context, node);
  psx_qual_type_t pointee = psx_semantic_type_table_pointee_value(
      ps_ctx_semantic_type_table_in(semantic_context), type.type_id);
  return (pointee.qualifiers & qualifier) == qualifier;
}

void ps_node_reject_const_assign_at_in(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const char *op, token_t *tok) {
  (void)op;
  if (!node) return;
  if (node->kind == ND_GENERIC_SELECTION) {
    node_generic_selection_t *selection =
        (node_generic_selection_t *)node;
    node_t *selected =
        generic_selection_semantic_expression(
            ps_ctx_resolution_store(semantic_context), selection);
    if (selected) {
      ps_node_reject_const_assign_at_in(
          semantic_context, diagnostics, selected, op, tok);
    }
    return;
  }
  psx_resolution_node_kind_t resolved_kind =
      psx_resolved_object_ref_node_kind(
          ps_ctx_resolution_store(semantic_context), node);
  if (resolved_kind == ND_LVAR || resolved_kind == ND_GVAR ||
      node->kind == ND_MEMBER_ACCESS ||
      node->kind == ND_UNARY_DEREF ||
      psx_resolution_node_kind(
          ps_ctx_resolution_store(semantic_context), node) == ND_DEREF) {
    /* 各再帰型ノードの qualifier はそのノード自身を修飾する。
     * ポインタ自身の const (`int * const p`) と pointee の const
     * (`const int *p`) は pointer node と base node に分かれている。 */
    if (node_semantic_self_has_qualifier(
            semantic_context, node, PSX_TYPE_QUALIFIER_CONST)) {
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CONST_ASSIGNMENT, tok,
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_CONST_ASSIGNMENT));
    }
  }
}

void ps_node_reject_const_qual_discard_at_in(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    token_t *tok) {
  if (!lhs || !rhs) return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_resolution_node_kind_t lhs_kind =
      psx_resolved_object_ref_node_kind(store, lhs);
  if (lhs_kind != ND_LVAR && lhs_kind != ND_GVAR) return;
  if (!ps_node_value_is_pointer_like(store, lhs)) return;
  if (ps_type_derived_function(ps_node_get_type(store, lhs)) &&
      ps_type_derived_function(ps_node_get_type(store, rhs))) {
    return;
  }
  if (node_semantic_pointee_has_qualifier(
          semantic_context, lhs, PSX_TYPE_QUALIFIER_CONST))
    return;
  if (node_semantic_pointee_has_qualifier(
          semantic_context, rhs, PSX_TYPE_QUALIFIER_CONST)) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_CONST_QUAL_DISCARD, tok,
        diag_message_for_in(
            diagnostics, DIAG_ERR_PARSER_CONST_QUAL_DISCARD));
  }
}

void ps_node_expect_lvalue_at_in(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const char *op, token_t *tok) {
  if (node && node->kind == ND_GENERIC_SELECTION) {
    node_generic_selection_t *selection =
        (node_generic_selection_t *)node;
    node_t *selected =
        generic_selection_semantic_expression(store, selection);
    if (selected) {
      ps_node_expect_lvalue_at_in(
          store, diagnostics, selected, op, tok);
      return;
    }
  }
  psx_resolution_node_kind_t resolved_kind =
      psx_resolved_object_ref_node_kind(store, node);
  if (!node || (resolved_kind != ND_LVAR &&
                resolved_kind != ND_MEMBER_ACCESS &&
                resolved_kind != ND_UNARY_DEREF &&
                resolved_kind != ND_SUBSCRIPT &&
                resolved_kind != ND_DEREF && resolved_kind != ND_GVAR)) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_LVALUE_REQUIRED, tok,
        diag_message_for_in(diagnostics, DIAG_ERR_PARSER_LVALUE_REQUIRED),
        (char *)op);
  }
}
