#include "node_utils.h"
#include "lvar_internal.h"
#include "decl.h"
#include "semantic_ctx.h"
#include "arena.h"
#include "../semantic/initializer_resolution.h"
#include "../semantic/record_decl_table.h"
#include "../type_layout.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static tk_float_kind_t semantic_shape_fp_kind(
    const psx_type_shape_t *shape);

typedef struct {
  int covered_union_off;
  int covered_union_size;
} psx_tag_flat_cover_state_t;

static void tag_flat_cover_state_init(psx_tag_flat_cover_state_t *state) {
  if (!state) return;
  state->covered_union_off = 0;
  state->covered_union_size = 0;
}

static int tag_flat_cover_state_covers(
    const psx_tag_flat_cover_state_t *state, int member_offset) {
  if (!state || state->covered_union_size <= 0) return 0;
  return member_offset >= state->covered_union_off &&
         member_offset < state->covered_union_off + state->covered_union_size;
}

int ps_gvar_decl_type_shape(const global_var_t *gv,
                            psx_type_shape_t *shape) {
  return gv && shape && psx_semantic_type_table_describe(
      gv->decl_type_table, gv->decl_qual_type.type_id, shape);
}

static int gvar_array_leaf_shape(const global_var_t *gv,
                                 psx_type_shape_t *shape) {
  if (!gv || !shape) return 0;
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      gv->decl_type_table, gv->decl_qual_type.type_id);
  return psx_semantic_type_table_describe(
      gv->decl_type_table, leaf.type_id, shape);
}

int ps_gvar_is_array(const global_var_t *gv) {
  psx_type_shape_t shape = {0};
  return ps_gvar_decl_type_shape(gv, &shape) &&
         shape.kind == PSX_TYPE_ARRAY;
}

int ps_gvar_is_struct_aggregate(const global_var_t *gv) {
  psx_type_shape_t shape = {0};
  return gvar_array_leaf_shape(gv, &shape) &&
         shape.kind == PSX_TYPE_STRUCT;
}

int ps_gvar_is_union_aggregate(const global_var_t *gv) {
  psx_type_shape_t shape = {0};
  return gvar_array_leaf_shape(gv, &shape) &&
         shape.kind == PSX_TYPE_UNION;
}

int ps_gvar_is_tag_aggregate(const global_var_t *gv) {
  return ps_gvar_is_struct_aggregate(gv) || ps_gvar_is_union_aggregate(gv);
}

static tk_float_kind_t gvar_initializer_fp_kind(const global_var_t *gv) {
  psx_type_shape_t shape = {0};
  return gvar_array_leaf_shape(gv, &shape)
             ? semantic_shape_fp_kind(&shape)
             : TK_FLOAT_KIND_NONE;
}

int ps_gvar_is_bool_scalar(const global_var_t *gv) {
  psx_type_shape_t shape = {0};
  return ps_gvar_decl_type_shape(gv, &shape) && shape.kind == PSX_TYPE_BOOL;
}

int ps_gvar_array_element_is_bool(const global_var_t *gv) {
  psx_type_shape_t root = {0};
  psx_type_shape_t leaf = {0};
  return ps_gvar_decl_type_shape(gv, &root) &&
         root.kind == PSX_TYPE_ARRAY &&
         gvar_array_leaf_shape(gv, &leaf) && leaf.kind == PSX_TYPE_BOOL;
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
  if (!ps_ctx_find_function_symbol_in(semantic_context, name, len)) {
    if (out_name) *out_name = NULL;
    if (out_len) *out_len = 0;
    return 0;
  }
  if (out_name) *out_name = name;
  if (out_len) *out_len = len;
  return 1;
}

static tk_float_kind_t semantic_shape_fp_kind(
    const psx_type_shape_t *shape) {
  if (!shape || (shape->kind != PSX_TYPE_FLOAT &&
                 shape->kind != PSX_TYPE_COMPLEX))
    return TK_FLOAT_KIND_NONE;
  switch (shape->floating_kind) {
    case PSX_FLOATING_KIND_FLOAT: return TK_FLOAT_KIND_FLOAT;
    case PSX_FLOATING_KIND_LONG_DOUBLE: return TK_FLOAT_KIND_LONG_DOUBLE;
    case PSX_FLOATING_KIND_DOUBLE: return TK_FLOAT_KIND_DOUBLE;
    case PSX_FLOATING_KIND_NONE: return TK_FLOAT_KIND_DOUBLE;
    default: return TK_FLOAT_KIND_NONE;
  }
}

psx_gvar_init_member_value_t
ps_gvar_init_member_value(const psx_semantic_type_table_t *semantic_types,
                          const global_var_t *gv, int idx,
                          const psx_record_member_decl_t *member,
                          int member_size) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  psx_type_shape_t member_shape = {0};
  int has_member_shape = psx_record_member_decl_leaf_shape(
      semantic_types, member, &member_shape);
  tk_float_kind_t member_fp_kind = has_member_shape
      ? semantic_shape_fp_kind(&member_shape)
      : TK_FLOAT_KIND_NONE;
  psx_gvar_init_member_value_t value = {
      .kind = PSX_GVAR_INIT_VALUE_INTEGER,
      .symbol_ref = gvar_init_slot_symbol_ref(&slot),
      .value = slot.value,
      .fvalue = slot.fvalue,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .size = member_size > 0 ? member_size : 0,
  };
  if (has_member_shape && member_shape.kind == PSX_TYPE_BOOL)
    value.value = value.value != 0;
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
  psx_type_shape_t shape = {0};
  return ps_gvar_decl_type_shape(gv, &shape) &&
         shape.kind == PSX_TYPE_ARRAY &&
         shape.array_len > 0
             ? shape.array_len
             : 0;
}

typedef struct {
  const global_var_t *gv;
  int index;
  int count;
} gvar_init_cursor_t;

typedef struct {
  int type_size;
  int elem_size;
  int elem_count;
  int is_array;
  int is_union;
  psx_type_id_t aggregate_type_id;
  const psx_record_decl_t *record_decl;
} gvar_aggregate_layout_t;

typedef struct {
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_target_info_t *target;
  psx_type_id_t aggregate_type_id;
  int ordinal;
  int count;
  const psx_record_decl_t *record_decl;
  psx_tag_flat_cover_state_t cover_state;
} gvar_aggregate_member_iter_t;

static gvar_aggregate_layout_t gvar_aggregate_layout(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_qual_type_t root_qual_type);
static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl);
static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      psx_record_member_decl_t *out_declaration,
                                      psx_record_member_layout_t *out_layout,
                                      int *out_ordinal);
static void gvar_aggregate_member_iter_set_next(gvar_aggregate_member_iter_t *iter,
                                                int next_ordinal);
static int gvar_walk_struct_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int struct_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user);
static int gvar_walk_union_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int union_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user);
static gvar_init_cursor_t gvar_init_cursor(const global_var_t *gv);
static int gvar_init_cursor_has(const gvar_init_cursor_t *cur);
static int gvar_init_cursor_index(const gvar_init_cursor_t *cur);
static int gvar_init_cursor_advance(gvar_init_cursor_t *cur);
static int gvar_init_cursor_advance_at_offset(
    gvar_init_cursor_t *cur, long long relative_offset);
static int gvar_init_cursor_consume_plain_zero_padding(gvar_init_cursor_t *cur,
                                                       int start_idx, int target_slots);
static int gvar_init_cursor_consume_resolved_type_zero_padding(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    gvar_init_cursor_t *cur, int start_idx);
static int gvar_init_cursor_pack_bitfield_unit(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    int member_index, long long base_offset, gvar_init_cursor_t *cur,
    psx_gvar_bitfield_unit_t *out);
static int record_union_init_member_for_slot(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_t *record_decl,
    const global_var_t *gv, psx_type_id_t aggregate_type_id,
    long long base_offset, int idx,
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
    psx_qual_type_t value_qual_type, const ag_target_info_t *target) {
  return psx_qual_type_layout_sizeof(
      semantic_types, record_layouts, value_qual_type,
      ag_target_info_data_layout(target));
}

static int gvar_member_storage_size_for_target(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    psx_qual_type_t member_qual_type, const ag_target_info_t *target) {
  return psx_qual_type_layout_sizeof(
      semantic_types, record_layouts, member_qual_type,
      ag_target_info_data_layout(target));
}

static int gvar_get_record_member_layout(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    int member_ordinal, psx_record_member_layout_t *out_layout) {
  psx_type_shape_t aggregate_shape = {0};
  if (!out_layout || !psx_semantic_type_table_describe(
                         semantic_types, aggregate_type_id,
                         &aggregate_shape) ||
      (aggregate_shape.kind != PSX_TYPE_STRUCT &&
       aggregate_shape.kind != PSX_TYPE_UNION) ||
      aggregate_shape.record_id == PSX_RECORD_ID_INVALID)
    return 0;
  const psx_record_layout_t *layout =
      psx_record_layout_table_lookup(record_layouts, aggregate_shape.record_id,
                                     ag_target_info_data_layout(target));
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
    const ag_target_info_t *target, psx_qual_type_t root_qual_type) {
  psx_type_id_t root_type_id = root_qual_type.type_id;
  int type_size = psx_qual_type_layout_sizeof(
      semantic_types, record_layouts, root_qual_type,
      ag_target_info_data_layout(target));
  psx_qual_type_t aggregate_type =
      psx_semantic_type_table_array_leaf(
          semantic_types, root_type_id);
  psx_type_id_t aggregate_type_id = aggregate_type.type_id;
  psx_type_shape_t aggregate_shape = {0};
  psx_type_shape_t root_shape = {0};
  int has_root_shape = psx_semantic_type_table_describe(
      semantic_types, root_type_id, &root_shape);
  int has_aggregate_shape = psx_semantic_type_table_describe(
      semantic_types, aggregate_type_id, &aggregate_shape) &&
      (aggregate_shape.kind == PSX_TYPE_STRUCT ||
       aggregate_shape.kind == PSX_TYPE_UNION) &&
      aggregate_shape.record_id != PSX_RECORD_ID_INVALID;
  gvar_aggregate_layout_t layout = {
      .type_size = type_size,
      .elem_size = type_size,
      .elem_count = 1,
      .is_array = has_root_shape && root_shape.kind == PSX_TYPE_ARRAY,
      .is_union = has_aggregate_shape &&
                  aggregate_shape.kind == PSX_TYPE_UNION,
      .aggregate_type_id = aggregate_type_id,
      .record_decl =
          has_aggregate_shape
              ? psx_record_decl_table_lookup(
                    record_decls, aggregate_shape.record_id)
              : NULL,
  };
  if (layout.is_array) {
    layout.elem_size = psx_qual_type_layout_sizeof(
        semantic_types, record_layouts, aggregate_type,
        ag_target_info_data_layout(target));
    layout.elem_count = psx_semantic_type_table_array_flat_element_count(
        semantic_types, root_type_id);
  }
  return layout;
}

static gvar_aggregate_member_iter_t gvar_aggregate_member_iter(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl) {
  gvar_aggregate_member_iter_t iter = {
      .semantic_types = semantic_types,
      .record_decls = record_decls,
      .record_layouts = record_layouts,
      .target = target,
      .aggregate_type_id = aggregate_type_id,
      .ordinal = 0,
      .count = record_decl ? record_decl->member_count : 0,
      .record_decl = record_decl,
  };
  tag_flat_cover_state_init(&iter.cover_state);
  return iter;
}

static int gvar_aggregate_member_next(gvar_aggregate_member_iter_t *iter,
                                      psx_record_member_decl_t *out_declaration,
                                      psx_record_member_layout_t *out_layout,
                                      int *out_ordinal) {
  if (!iter || !out_declaration || !out_layout) return 0;
  while (iter->ordinal < iter->count) {
    int ordinal = iter->ordinal++;
    if (!iter->record_decl || !iter->record_decl->members) return 0;
    psx_record_member_decl_t declaration = {0};
    psx_record_member_layout_t layout = {0};
    declaration = iter->record_decl->members[ordinal];
    if (psx_record_member_decl_is_unnamed_struct(
            iter->semantic_types, &declaration))
      continue;
    if (!gvar_get_record_member_layout(
            iter->semantic_types, iter->record_layouts, iter->target,
            iter->aggregate_type_id, ordinal, &layout))
      return 0;
    if (tag_flat_cover_state_covers(
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
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_member_decl_t *member) {
  psx_type_shape_t shape = {0};
  return psx_record_member_decl_leaf_shape(
             semantic_types, member, &shape) &&
         (shape.kind == PSX_TYPE_STRUCT || shape.kind == PSX_TYPE_UNION) &&
         shape.record_id != PSX_RECORD_ID_INVALID
             ? psx_record_decl_table_lookup(
                   record_decls, shape.record_id)
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
  psx_type_shape_t aggregate_shape = {0};
  if (!record_decl || !psx_semantic_type_table_describe(
                          semantic_types, aggregate_type_id,
                          &aggregate_shape) ||
      (aggregate_shape.kind != PSX_TYPE_STRUCT &&
       aggregate_shape.kind != PSX_TYPE_UNION) ||
      aggregate_shape.record_id == PSX_RECORD_ID_INVALID)
    return 0;
  const psx_record_layout_t *layout =
      psx_record_layout_table_lookup(record_layouts, aggregate_shape.record_id,
                                     ag_target_info_data_layout(target));
  if (!layout) return 0;
  int member_count = record_decl->member_count;
  if (layout->member_count < member_count) member_count = layout->member_count;
  for (int i = 0; i < member_count; i++) {
    const psx_record_member_decl_t *member = &record_decl->members[i];
    psx_record_member_layout_t member_layout = {0};
    if (!psx_record_member_decl_is_unnamed_aggregate(
            semantic_types, member) ||
        !gvar_get_record_member_layout(
            semantic_types, record_layouts, target, aggregate_type_id,
            i, &member_layout))
      continue;
    psx_qual_type_t member_qual_type =
        psx_semantic_type_table_record_member(
            semantic_types, aggregate_type_id, i);
    psx_type_id_t member_type_id = member_qual_type.type_id;
    int member_size = psx_qual_type_layout_sizeof(
        semantic_types, record_layouts, member_qual_type,
        ag_target_info_data_layout(target));
    int start = base_offset + member_layout.offset;
    if (member_size <= 0 || target_offset < start ||
        target_offset >= start + member_size)
      continue;
    if (psx_record_member_decl_is_unnamed_union(semantic_types, member)) {
      if (out_offset) *out_offset = start;
      if (out_size) *out_size = member_size;
      return 1;
    }
    psx_type_id_t child_type_id = psx_semantic_type_table_array_leaf(
        semantic_types, member_type_id).type_id;
    if (gvar_resolved_record_find_unnamed_union_covering_offset(
            semantic_types, record_decls, record_layouts, target,
            child_type_id,
            gvar_member_declaration_record_decl(
                semantic_types, record_decls, member), start,
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
  psx_qual_type_t member_qual_type =
      psx_semantic_type_table_record_member(
          iter->semantic_types, iter->aggregate_type_id,
          member_ordinal);
  if (psx_record_member_decl_is_unnamed_union(
          iter->semantic_types, declaration)) {
    int member_size = psx_qual_type_layout_sizeof(
        iter->semantic_types, iter->record_layouts, member_qual_type,
        ag_target_info_data_layout(iter->target));
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

static psx_qual_type_t gvar_member_qual_type(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t aggregate_type_id, int member_ordinal) {
  return psx_semantic_type_table_record_member(
      semantic_types, aggregate_type_id, member_ordinal);
}

static psx_qual_type_t gvar_member_value_qual_type(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t aggregate_type_id, int member_ordinal) {
  psx_qual_type_t member_qual_type = gvar_member_qual_type(
      semantic_types, aggregate_type_id, member_ordinal);
  psx_type_shape_t member_shape = {0};
  return psx_semantic_type_table_describe(
             semantic_types, member_qual_type.type_id, &member_shape) &&
         member_shape.kind == PSX_TYPE_ARRAY
             ? psx_semantic_type_table_array_leaf(
                   semantic_types, member_qual_type.type_id)
             : member_qual_type;
}

static psx_type_id_t gvar_member_value_type_id(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t aggregate_type_id, int member_ordinal) {
  return gvar_member_value_qual_type(
      semantic_types, aggregate_type_id, member_ordinal).type_id;
}

static int gvar_walk_struct_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int struct_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!cur || !record_decl || !record_decl->members) return 0;
  int prev_end = 0;
  gvar_aggregate_member_iter_t iter =
      gvar_aggregate_member_iter(
          semantic_types, record_decls, record_layouts, target,
          aggregate_type_id, record_decl);
  while (gvar_init_cursor_has(cur)) {
    psx_record_member_decl_t member = {0};
    psx_record_member_layout_t member_layout = {0};
    int ordinal = 0;
    if (!gvar_aggregate_member_next(
            &iter, &member, &member_layout, &ordinal))
      break;
    psx_qual_type_t member_qual_type = gvar_member_qual_type(
        semantic_types, aggregate_type_id, ordinal);
    psx_type_id_t member_type_id = member_qual_type.type_id;
    psx_qual_type_t member_value_qual_type = gvar_member_value_qual_type(
        semantic_types, aggregate_type_id, ordinal);
    psx_type_id_t member_value_type_id = member_value_qual_type.type_id;
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
              semantic_types, record_layouts, target, aggregate_type_id,
              record_decl, ordinal, base_offset, cur, &unit)) {
        return 0;
      }
      ops->bitfield_unit(user, &unit, base_offset);
      gvar_aggregate_member_iter_set_next(&iter, unit.last_member_index + 1);
      prev_end = unit.offset + unit.size;
      continue;
    }
    int member_value_size =
        gvar_member_value_size_for_target(
            semantic_types, record_layouts, member_value_qual_type, target);
    int member_storage_size =
        gvar_member_storage_size_for_target(
            semantic_types, record_layouts, member_qual_type, target);
    int member_array_count =
        psx_semantic_type_table_array_flat_element_count(
            semantic_types, member.decl_qual_type.type_id);
    const psx_record_decl_t *member_record =
        gvar_member_declaration_record_decl(
            semantic_types, record_decls, &member);
    if (member_array_count > 0) {
      if (psx_record_member_decl_is_tag_aggregate(
              semantic_types, &member)) {
        for (int k = 0; k < member_array_count; k++) {
          if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
          int elem_start_idx = gvar_init_cursor_index(cur);
          long long elem_off = base_offset + member_layout.offset +
                               (long long)k * member_value_size;
          int ok = psx_record_member_decl_is_union_aggregate(
                       semantic_types, &member)
              ? gvar_walk_union_initializer(semantic_types,
                                            record_decls,
                                            record_layouts,
                                            target,
                                            member_aggregate_type_id,
                                            member_record,
                                            gv, cur, elem_off, member_value_size,
                                            ops, user)
              : gvar_walk_struct_initializer(semantic_types,
                                             record_decls,
                                             record_layouts,
                                             target,
                                             member_aggregate_type_id,
                                             member_record,
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
          int slot = gvar_init_cursor_advance_at_offset(cur, elem_off);
          if (slot < 0) return 0;
          gvar_walk_emit_scalar(
              ops, user, &member, member_value_type_id, slot, elem_off);
        }
      }
      prev_end = member_layout.offset + member_storage_size;
      continue;
    }
    if (psx_record_member_decl_is_struct_aggregate(
            semantic_types, &member)) {
      int member_start_idx = gvar_init_cursor_index(cur);
      if (!gvar_walk_struct_initializer(semantic_types,
                                        record_decls,
                                        record_layouts,
                                        target,
                                        member_aggregate_type_id,
                                        member_record,
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
    if (psx_record_member_decl_is_union_aggregate(
            semantic_types, &member)) {
      if (!gvar_walk_union_initializer(semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       member_aggregate_type_id,
                                       member_record,
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
    long long scalar_offset = base_offset + member_layout.offset;
    int slot = gvar_init_cursor_advance_at_offset(cur, scalar_offset);
    if (slot < 0) return 0;
    gvar_walk_emit_scalar(
        ops, user, &member, member_value_type_id, slot,
        scalar_offset);
    prev_end = member_layout.offset + member_value_size;
  }
  if (prev_end < struct_size) {
    gvar_walk_emit_padding(ops, user, base_offset + prev_end, struct_size - prev_end);
  }
  return 1;
}

static int gvar_walk_union_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    global_var_t *gv, gvar_init_cursor_t *cur,
    long long base_offset, int union_size,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!record_decl || !record_decl->members) return 0;
  if (!gvar_init_cursor_has(cur)) {
    gvar_walk_emit_padding(ops, user, base_offset, union_size);
    return 1;
  }
  int start_idx = gvar_init_cursor_index(cur);
  psx_record_member_decl_t member = {0};
  psx_record_member_layout_t member_layout = {0};
  int member_ordinal = -1;
  if (!record_union_init_member_for_slot(
          semantic_types, record_decl, gv, aggregate_type_id,
          base_offset, gvar_init_cursor_index(cur),
          &member, &member_ordinal)) {
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
  psx_qual_type_t member_qual_type = gvar_member_qual_type(
      semantic_types, aggregate_type_id, member_ordinal);
  psx_type_id_t member_type_id = member_qual_type.type_id;
  psx_qual_type_t member_value_qual_type = gvar_member_value_qual_type(
      semantic_types, aggregate_type_id, member_ordinal);
  psx_type_id_t member_value_type_id = member_value_qual_type.type_id;
  psx_type_id_t member_aggregate_type_id =
      psx_semantic_type_table_array_leaf(
          semantic_types, member_type_id).type_id;
  int member_value_size =
      gvar_member_value_size_for_target(
          semantic_types, record_layouts, member_value_qual_type, target);
  int member_storage_size =
      gvar_member_storage_size_for_target(
          semantic_types, record_layouts, member_qual_type, target);
  int member_array_count =
      psx_semantic_type_table_array_flat_element_count(
          semantic_types, member.decl_qual_type.type_id);
  int emitted = member_array_count > 0 ? member_storage_size : member_value_size;
  const psx_record_decl_t *member_record =
      gvar_member_declaration_record_decl(
          semantic_types, record_decls, &member);
  if (member_layout.offset > 0)
    gvar_walk_emit_padding(ops, user, base_offset, member_layout.offset);
  if (member.bit_width > 0) {
    if (!gvar_walk_require_bitfield_member(ops)) return 0;
    long long bitfield_offset = base_offset + member_layout.offset;
    int slot = gvar_init_cursor_advance_at_offset(cur, bitfield_offset);
    if (slot < 0) return 0;
    gvar_walk_emit_bitfield_member(
        ops, user, &member, &member_layout, member_value_type_id, slot,
        bitfield_offset);
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
    if (psx_record_member_decl_is_tag_aggregate(semantic_types, &member)) {
      for (int k = 0; k < member_array_count; k++) {
        if (!gvar_init_cursor_has(cur) && !gvar_walk_needs_padding(ops)) break;
        int elem_start_idx = gvar_init_cursor_index(cur);
        long long elem_off = base_offset + member_layout.offset +
                             (long long)k * member_value_size;
        int ok = psx_record_member_decl_is_struct_aggregate(
                     semantic_types, &member)
            ? gvar_walk_struct_initializer(semantic_types,
                                           record_decls,
                                           record_layouts,
                                           target,
                                           member_aggregate_type_id,
                                           member_record,
                                           gv, cur, elem_off, member_value_size,
                                           ops, user)
            : gvar_walk_union_initializer(semantic_types,
                                          record_decls,
                                          record_layouts,
                                          target,
                                          member_aggregate_type_id,
                                          member_record,
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
        int slot = gvar_init_cursor_advance_at_offset(cur, elem_off);
        if (slot < 0) return 0;
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
  if (psx_record_member_decl_is_tag_aggregate(semantic_types, &member)) {
    int ok = psx_record_member_decl_is_struct_aggregate(
                 semantic_types, &member)
        ? gvar_walk_struct_initializer(semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       member_aggregate_type_id,
                                       member_record,
                                       gv, cur, base_offset + member_layout.offset,
                                       member_value_size, ops, user)
        : gvar_walk_union_initializer(semantic_types,
                                      record_decls,
                                      record_layouts,
                                      target,
                                      member_aggregate_type_id,
                                      member_record,
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
  long long scalar_offset = base_offset + member_layout.offset;
  int slot = gvar_init_cursor_advance_at_offset(cur, scalar_offset);
  if (slot < 0) return 0;
  gvar_walk_emit_scalar(
      ops, user, &member, member_value_type_id, slot,
      scalar_offset);
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
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_qual_type_t root_qual_type,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  gvar_aggregate_layout_t layout =
      gvar_aggregate_layout(
          semantic_types, record_decls, record_layouts, target,
          root_qual_type);
  if (!layout.record_decl) return 0;
  gvar_init_cursor_t cur = gvar_init_cursor(gv);
  if (!layout.is_array) {
    return layout.is_union
        ? gvar_walk_union_initializer(semantic_types,
                                      record_decls,
                                      record_layouts,
                                      target,
                                      layout.aggregate_type_id,
                                      layout.record_decl,
                                      gv, &cur, base_offset, layout.type_size,
                                      ops, user)
        : gvar_walk_struct_initializer(semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       layout.aggregate_type_id,
                                       layout.record_decl,
                                       gv, &cur, base_offset, layout.type_size,
                                       ops, user);
  }
  for (int e = 0; e < layout.elem_count; e++) {
    if (!gvar_init_cursor_has(&cur) && !gvar_walk_needs_padding(ops)) break;
    long long elem_off = base_offset + (long long)e * layout.elem_size;
    if (layout.is_union) {
      if (!gvar_walk_union_initializer(semantic_types,
                                       record_decls,
                                       record_layouts,
                                       target,
                                       layout.aggregate_type_id,
                                       layout.record_decl,
                                       gv, &cur, elem_off, layout.elem_size,
                                       ops, user)) {
        return 0;
      }
    } else {
      int elem_start_idx = gvar_init_cursor_index(&cur);
      if (!gvar_walk_struct_initializer(semantic_types,
                                        record_decls,
                                        record_layouts,
                                        target,
                                        layout.aggregate_type_id,
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
      ps_ctx_semantic_type_table_in(semantic_context),
      ps_ctx_record_decl_table_in(semantic_context),
      ps_ctx_record_layout_table_in(semantic_context),
      ps_ctx_target_info(semantic_context), ps_gvar_decl_qual_type(gv),
      gv, base_offset, ops, user);
}

int ps_gvar_walk_resolved_aggregate_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_qual_type_t root_qual_type,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  if (!semantic_types || !record_decls || !record_layouts || !target ||
      root_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return gvar_walk_aggregate_initializer(
      semantic_types, record_decls, record_layouts, target, root_qual_type,
      gv, base_offset, ops, user);
}

psx_gvar_init_slot_t ps_gvar_init_slot_view(const global_var_t *gv, int idx) {
  psx_gvar_init_slot_t slot = {0};
  if (!gv || idx < 0 || idx >= gv->init_count) return slot;
  slot.in_range = 1;
  slot.relative_offset = gv->init_offsets ? gv->init_offsets[idx] : -1;
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

static int gvar_init_cursor_advance_at_offset(
    gvar_init_cursor_t *cur, long long relative_offset) {
  if (!gvar_init_cursor_has(cur)) return -1;
  if (!cur->gv || !cur->gv->init_offsets || relative_offset < 0 ||
      relative_offset > INT32_MAX ||
      cur->gv->init_offsets[cur->index] < 0)
    return gvar_init_cursor_advance(cur);
  int target = (int)relative_offset;
  while (gvar_init_cursor_has(cur) &&
         cur->gv->init_offsets[cur->index] < target)
    cur->index++;
  if (!gvar_init_cursor_has(cur) ||
      cur->gv->init_offsets[cur->index] != target)
    return -1;
  return gvar_init_cursor_advance(cur);
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

static int gvar_init_cursor_consume_resolved_type_zero_padding(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    gvar_init_cursor_t *cur, int start_idx) {
  return gvar_init_cursor_consume_plain_zero_padding(
      cur, start_idx,
      psx_initializer_flat_slot_count_with_records(
          semantic_types, record_decls, record_layouts,
          ag_target_info_data_layout(target), aggregate_type_id));
}

unsigned long long ps_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset) {
  psx_gvar_init_slot_t slot = ps_gvar_init_slot_view(gv, idx);
  unsigned long long mask = bit_width >= 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
  return ((unsigned long long)slot.value & mask) << bit_offset;
}

static int gvar_init_cursor_pack_bitfield_unit(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record_decl,
    int member_index, long long base_offset, gvar_init_cursor_t *cur,
    psx_gvar_bitfield_unit_t *out) {
  if (!cur || !out || !record_decl || !record_decl->members) return 0;
  psx_record_member_decl_t first = {0};
  psx_record_member_layout_t first_layout = {0};
  int n_members = record_decl->member_count;
  if (member_index < 0 || member_index >= n_members) return 0;
  first = record_decl->members[member_index];
  if (!gvar_get_record_member_layout(
          semantic_types, record_layouts, target, aggregate_type_id,
          member_index, &first_layout))
    return 0;
  if (first.bit_width <= 0) return 0;
  int unit_off = first_layout.offset;
  psx_type_id_t unit_type_id = gvar_member_value_type_id(
      semantic_types, aggregate_type_id, member_index);
  int unit_size =
      psx_type_layout_sizeof(semantic_types, record_layouts, unit_type_id,
                        ag_target_info_data_layout(target));
  if (unit_size <= 0) return 0;
  unsigned long long packed = 0;
  int m = member_index;
  int last = member_index;
  if (cur->gv && cur->gv->init_offsets &&
      cur->index >= 0 && cur->index < cur->count &&
      cur->gv->init_offsets[cur->index] >= 0) {
    int slot = gvar_init_cursor_advance_at_offset(
        cur, base_offset + unit_off);
    if (slot < 0) return 0;
    packed = (unsigned long long)ps_gvar_init_slot_view(
        cur->gv, slot).value;
    while (m < n_members) {
      psx_record_member_layout_t member_layout = {0};
      const psx_record_member_decl_t *member = &record_decl->members[m];
      if (!gvar_get_record_member_layout(
              semantic_types, record_layouts, target, aggregate_type_id,
              m, &member_layout) ||
          member->bit_width <= 0 || member_layout.offset != unit_off)
        break;
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
  while (m < n_members && gvar_init_cursor_has(cur)) {
    psx_record_member_decl_t member = {0};
    psx_record_member_layout_t member_layout = {0};
    member = record_decl->members[m];
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
  gv->init_offsets = malloc((size_t)cap * sizeof(int));
  if (with_fvalues) gv->init_fvalues = calloc((size_t)cap, sizeof(double));
  for (int i = 0; i < cap; i++) ps_gvar_init_slot_clear(gv, i);
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
    gv->init_offsets = realloc(gv->init_offsets, (size_t)new_cap * sizeof(int));
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
  if (gv->init_offsets) gv->init_offsets[idx] = -1;
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

void ps_gvar_init_slot_set_offset(
    global_var_t *gv, int idx, int relative_offset) {
  if (!gv || idx < 0 || !gv->init_offsets) return;
  gv->init_offsets[idx] = relative_offset;
}

int ps_gvar_union_activation_set(
    global_var_t *gv, psx_type_id_t union_type_id,
    int relative_offset, int member_ordinal) {
  if (!gv || union_type_id == PSX_TYPE_ID_INVALID ||
      relative_offset < 0 || member_ordinal < 0)
    return 0;
  for (int i = 0; i < gv->init_union_activation_count; i++) {
    psx_gvar_union_activation_t *activation =
        &gv->init_union_activations[i];
    if (activation->union_type_id == union_type_id &&
        activation->relative_offset == relative_offset) {
      activation->member_ordinal = member_ordinal;
      return 1;
    }
  }
  if (gv->init_union_activation_count ==
      gv->init_union_activation_capacity) {
    int capacity = gv->init_union_activation_capacity
                       ? gv->init_union_activation_capacity * 2
                       : 8;
    psx_gvar_union_activation_t *activations = realloc(
        gv->init_union_activations,
        (size_t)capacity * sizeof(*activations));
    if (!activations) return 0;
    gv->init_union_activations = activations;
    gv->init_union_activation_capacity = capacity;
  }
  gv->init_union_activations[
      gv->init_union_activation_count++] =
      (psx_gvar_union_activation_t){
          .union_type_id = union_type_id,
          .relative_offset = relative_offset,
          .member_ordinal = member_ordinal,
      };
  return 1;
}

int ps_gvar_union_activation_ordinal(
    const global_var_t *gv, psx_type_id_t union_type_id,
    int relative_offset, int *member_ordinal) {
  if (!gv || union_type_id == PSX_TYPE_ID_INVALID ||
      relative_offset < 0 || !member_ordinal)
    return 0;
  for (int i = 0; i < gv->init_union_activation_count; i++) {
    const psx_gvar_union_activation_t *activation =
        &gv->init_union_activations[i];
    if (activation->union_type_id == union_type_id &&
        activation->relative_offset == relative_offset) {
      *member_ordinal = activation->member_ordinal;
      return 1;
    }
  }
  return 0;
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
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_member_decl_t *declaration) {
  psx_type_shape_t shape = {0};
  tk_float_kind_t fp_kind =
      psx_record_member_decl_leaf_shape(
          semantic_types, declaration, &shape)
          ? semantic_shape_fp_kind(&shape)
          : TK_FLOAT_KIND_NONE;
  return fp_kind == TK_FLOAT_KIND_FLOAT ? 4
       : fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 0;
}

static int record_union_init_member_for_slot(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_t *record_decl,
    const global_var_t *gv, psx_type_id_t aggregate_type_id,
    long long base_offset, int idx,
    psx_record_member_decl_t *out_declaration,
    int *out_ordinal) {
  if (!semantic_types || !record_decl || !record_decl->members ||
      !out_declaration)
    return 0;
  int ordinal = -1;
  int has_activation =
      base_offset >= 0 && base_offset <= INT32_MAX &&
      ps_gvar_union_activation_ordinal(
          gv, aggregate_type_id, (int)base_offset, &ordinal);
  if (!has_activation)
    ordinal = ps_gvar_union_init_slot_ordinal(gv, idx);
  if (ordinal < 0 || ordinal >= record_decl->member_count) return 0;
  *out_declaration = record_decl->members[ordinal];
  if (has_activation) {
    if (out_ordinal) *out_ordinal = ordinal;
    return 1;
  }
  int init_fp_size = ps_gvar_union_init_slot_fp_size(gv, idx);
  int selected_fp_size = record_member_decl_fp_size(
      semantic_types, out_declaration);
  if (init_fp_size != selected_fp_size &&
      !(init_fp_size == 0 && selected_fp_size == 0)) {
    for (int i = 0; i < record_decl->member_count; i++) {
      psx_record_member_decl_t candidate = record_decl->members[i];
      int candidate_fp_size = record_member_decl_fp_size(
          semantic_types, &candidate);
      if ((init_fp_size > 0 && candidate_fp_size == init_fp_size) ||
          (init_fp_size == 0 && candidate_fp_size == 0)) {
        *out_declaration = candidate;
        ordinal = i;
        break;
      }
    }
  }
  if (out_ordinal) *out_ordinal = ordinal;
  return 1;
}

psx_qual_type_t ps_gvar_decl_qual_type(const global_var_t *gv) {
  return gv ? gv->decl_qual_type
            : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                PSX_TYPE_QUALIFIER_NONE};
}

psx_type_id_t ps_gvar_decl_type_id(const global_var_t *gv) {
  return ps_gvar_decl_qual_type(gv).type_id;
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

node_t *psx_node_new_syntax_int_in(
    arena_context_t *arena_context, long long val, token_t *tok) {
  node_num_t *node = arena_alloc_in(arena_context, sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->base.tok = tok;
  node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  node->val = val;
  return (node_t *)node;
}

node_t *psx_node_new_source_cast_in(
    arena_context_t *arena_context,
    node_t *operand, psx_type_name_ref_t type_name) {
  node_source_cast_t *cast = arena_alloc_in(
      arena_context, sizeof(node_source_cast_t));
  cast->base.kind = ND_SOURCE_CAST;
  cast->base.lhs = operand;
  cast->type_name = type_name;
  return (node_t *)cast;
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
  result->kind = ND_ADDRESS_OF;
  result->lhs = operand;
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

node_t *psx_node_new_raw_assign_in(arena_context_t *arena_context,
                                   node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc_in(arena_context, sizeof(node_t));
  node->kind = ND_ASSIGN;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

node_t *psx_node_new_null_statement_syntax_in(
    arena_context_t *arena_context, token_t *token) {
  node_t *node = arena_alloc_in(arena_context, sizeof(*node));
  if (!node) return NULL;
  node->kind = ND_NULL_STMT;
  node->tok = token;
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
    psx_type_name_ref_t type_name, node_t *initializer, token_t *tok) {
  node_compound_literal_t *node =
      arena_alloc_in(arena_context, sizeof(node_compound_literal_t));
  node->base.kind = ND_COMPOUND_LITERAL;
  node->base.rhs = initializer;
  node->base.tok = tok;
  node->type_name = type_name;
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
