#include "static_data_initializer.h"
#include "runtime_context.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../parser/node_utils.h"
#include "../semantic/constant_expression.h"
#include "../semantic/initializer_resolution.h"
#include "../semantic/static_initializer_resolution.h"
#include "../tokenizer/literals.h"
#include "../type_layout.h"

typedef struct {
  psx_lowering_context_t *lowering_context;
  global_var_t *global;
  psx_initializer_scalar_leaf_list_t leaves;
  token_t *fallback_tok;
} static_array_lowering_t;

static ag_diagnostic_context_t *diagnostics(
    const static_array_lowering_t *lowering) {
  return ps_lowering_diagnostics(lowering->lowering_context);
}

static int lowering_type_size(
    const psx_lowering_context_t *lowering_context,
    const psx_type_t *type) {
  return ps_type_sizeof_id_with_records(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_layouts(lowering_context),
      ps_lowering_type_id(lowering_context, type),
      ps_lowering_target(lowering_context));
}

static int type_size(
    const static_array_lowering_t *lowering, const psx_type_t *type) {
  return lowering_type_size(lowering->lowering_context, type);
}

static const psx_record_decl_t *record_decl(
    const psx_lowering_context_t *lowering_context,
    const psx_type_t *type) {
  return lowering_context && type
             ? psx_record_decl_table_lookup(
                   ps_lowering_record_decls(lowering_context),
                   ps_type_record_id(type))
             : NULL;
}

static const psx_record_member_layout_t *record_member_layout(
    const static_array_lowering_t *lowering,
    const psx_type_t *aggregate_type, int member_index) {
  const psx_lowering_context_t *context = lowering
                                              ? lowering->lowering_context
                                              : NULL;
  if (!context || !aggregate_type ||
      aggregate_type->record_id == PSX_RECORD_ID_INVALID)
    return NULL;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      ps_lowering_record_layouts(context), aggregate_type->record_id,
      ps_lowering_target(context));
  return psx_record_layout_member(layout, member_index);
}

static int record_member_offset(
    const static_array_lowering_t *lowering,
    const psx_type_t *aggregate_type, int member_index) {
  const psx_record_member_layout_t *layout = record_member_layout(
      lowering, aggregate_type, member_index);
  return layout ? layout->offset : -1;
}

static psx_initializer_member_ref_t initializer_member_ref(
    const static_array_lowering_t *lowering,
    const psx_type_t *aggregate_type, int member_index,
    const tag_member_info_t *declaration) {
  psx_initializer_member_ref_t ref = {
      .declaration = declaration,
      .record_id = ps_type_record_id(aggregate_type),
      .member_index = member_index,
  };
  const psx_record_member_layout_t *layout = record_member_layout(
      lowering, aggregate_type, member_index);
  if (layout) ref.layout = *layout;
  return ref;
}

static int leaf_index_at_offset(
    const psx_initializer_scalar_leaf_list_t *leaves, int offset) {
  if (!leaves) return -1;
  for (int i = 0; i < leaves->count; i++) {
    if (leaves->items[i].relative_offset == offset) return i;
  }
  return -1;
}

static int leaf_index_for_target(
    const psx_initializer_scalar_leaf_list_t *leaves,
    const psx_initializer_target_t *target) {
  if (!leaves || !target) return -1;
  if (target->member_ref.declaration) {
    for (int i = 0; i < leaves->count; i++) {
      if (leaves->items[i].relative_offset == target->relative_offset &&
          leaves->items[i].member_ref.record_id ==
              target->member_ref.record_id &&
          leaves->items[i].member_ref.member_index ==
              target->member_ref.member_index)
        return i;
    }
  }
  return leaf_index_at_offset(leaves, target->relative_offset);
}

static psx_initializer_target_t positional_target(
    const static_array_lowering_t *lowering,
    const psx_type_t *context_type, int context_offset,
    const psx_initializer_scalar_leaf_list_t *leaves, int cursor,
    int preserve_subobject) {
  psx_initializer_target_t target = {
      .first_array_index = -1,
      .first_member_index = -1,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  if (!context_type || !leaves || cursor < 0 || cursor >= leaves->count)
    return target;
  const psx_initializer_scalar_leaf_t *leaf = &leaves->items[cursor];
  target.type = leaf->type;
  target.type_id = leaf->type_id;
  target.relative_offset = leaf->relative_offset;
  target.member_ref = leaf->member_ref;
  const psx_record_decl_t *record = record_decl(
      lowering->lowering_context, context_type);
  if (context_type->kind == PSX_TYPE_UNION &&
      record && record->member_count > 0) {
    const tag_member_info_t *member = &record->members[0];
    if (preserve_subobject) {
      target.type = ps_tag_member_decl_type(member);
      target.type_id = ps_lowering_type_id(
          lowering->lowering_context, target.type);
      target.relative_offset = context_offset +
                               record_member_offset(
                                   lowering, context_type, 0);
      target.member_ref = initializer_member_ref(
          lowering, context_type, 0, member);
    }
    target.first_member_index = 0;
    target.union_relative_offset = context_offset;
    target.union_member_index = 0;
    return target;
  }
  if (!preserve_subobject) return target;
  if (context_type->kind == PSX_TYPE_STRUCT && record) {
    for (int i = 0; i < record->member_count; i++) {
      const tag_member_info_t *member = &record->members[i];
      const psx_type_t *member_type = ps_tag_member_decl_type(member);
      int member_offset = context_offset +
                          record_member_offset(
                              lowering, context_type, i);
      if (member_offset != leaf->relative_offset || !member_type) continue;
      if (member_type->kind != PSX_TYPE_ARRAY &&
          !ps_type_is_tag_aggregate(member_type)) continue;
      target.type = member_type;
      target.type_id = ps_lowering_type_id(
          lowering->lowering_context, member_type);
      target.relative_offset = member_offset;
      target.member_ref = initializer_member_ref(
          lowering, context_type, i, member);
      target.first_member_index = i;
      return target;
    }
    return target;
  }
  if (context_type->kind != PSX_TYPE_ARRAY || !context_type->base)
    return target;

  int child_size = type_size(lowering, context_type->base);
  if (child_size <= 0 || leaf->relative_offset < context_offset) return target;
  int child_index = (leaf->relative_offset - context_offset) / child_size;
  int child_offset = context_offset + child_index * child_size;
  if (child_index < 0 || child_index >= context_type->array_len ||
      child_offset != leaf->relative_offset) return target;
  target.type = context_type->base;
  target.type_id = ps_lowering_type_id(
      lowering->lowering_context, target.type);
  target.relative_offset = child_offset;
  target.member_ref = (psx_initializer_member_ref_t){0};
  target.first_array_index = child_index;
  return target;
}

static void mark_union_target(
    static_array_lowering_t *lowering,
    const psx_initializer_target_t *target) {
  if (!lowering || !target || target->union_relative_offset < 0 ||
      target->union_member_index < 0) return;
  int index = leaf_index_at_offset(
      &lowering->leaves, target->union_relative_offset);
  if (index >= 0)
    ps_gvar_init_slot_set_ordinal(
        lowering->global, index, target->union_member_index);
}

static void write_scalar_value(
    static_array_lowering_t *lowering,
    const psx_initializer_target_t *target, node_t *value,
    token_t *tok) {
  int index = leaf_index_for_target(&lowering->leaves, target);
  if (index < 0 || index >= lowering->leaves.count) {
    ps_diag_ctx_in(
        diagnostics(lowering), tok ? tok : lowering->fallback_tok,
        "static-init", "%s",
        diag_message_for_in(
            diagnostics(lowering),
                     DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  }
  const psx_type_t *type = target->type;
  char *symbol = NULL;
  int symbol_len = 0;
  long long integer = 0;
  double floating = 0.0;
  long long offset = 0;
  if (type && (type->kind == PSX_TYPE_POINTER ||
               type->kind == PSX_TYPE_FUNCTION)) {
    if (psx_resolve_static_address_constant(
            value, &symbol, &symbol_len, &offset)) {
      integer = offset;
    }
  } else if (type && type->kind == PSX_TYPE_FLOAT) {
    int ok = 1;
    floating = psx_eval_const_fp(value, &ok);
    if (!ok) floating = 0.0;
  } else {
    int ok = 1;
    integer = psx_eval_const_int(value, &ok);
    if (!ok) integer = 0;
    if (type && type->kind == PSX_TYPE_BOOL) integer = integer != 0;
  }
  ps_gvar_init_slot_write(
      lowering->global, index, integer, floating, symbol, symbol_len);
  if (!symbol && type && type->kind == PSX_TYPE_FLOAT &&
      target->union_member_index >= 0) {
    ps_gvar_init_slot_write_fp_sentinel(
        lowering->global, index, ps_type_floating_token_kind(type),
        type_size(lowering, type));
  }
}

static void write_string_value(
    static_array_lowering_t *lowering, const psx_type_t *array_type,
    int relative_offset, node_string_t *string, token_t *tok) {
  const psx_type_t *element = ps_type_array_leaf_type(array_type);
  int element_size = type_size(lowering, element);
  int total_size = type_size(lowering, array_type);
  int capacity = element_size > 0 ? total_size / element_size : 0;
  int start = leaf_index_at_offset(&lowering->leaves, relative_offset);
  int char_width = (int)string->char_width;
  if (char_width <= 0) char_width = 1;
  if (!element || capacity <= 0 || start < 0 || element_size != char_width) {
    ps_diag_ctx_in(
        diagnostics(lowering), tok ? tok : lowering->fallback_tok,
        "static-init", "%s",
        diag_message_for_in(
            diagnostics(lowering),
                     DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  }
  if (!string->literal_contents) {
    ps_diag_ctx_in(
        diagnostics(lowering), tok ? tok : lowering->fallback_tok,
        "static-init", "%s",
        diag_message_for_in(
            diagnostics(lowering),
                     DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }
  ps_gvar_init_slots_write_string_units(
      lowering->global, start, string->literal_contents,
      string->literal_length,
      element_size, capacity);
}

static int is_character_array_for_string(
    const static_array_lowering_t *lowering,
    const psx_type_t *type, const node_string_t *string) {
  if (!type || type->kind != PSX_TYPE_ARRAY || !string) return 0;
  const psx_type_t *element = ps_type_array_leaf_type(type);
  if (!element || element->kind == PSX_TYPE_POINTER ||
      element->kind == PSX_TYPE_FUNCTION ||
      ps_type_is_tag_aggregate(element)) return 0;
  int width = (int)string->char_width;
  if (width <= 0) width = 1;
  return type_size(lowering, element) == width;
}

static void lower_array_list(
    static_array_lowering_t *lowering, const psx_type_t *context_type,
    int context_offset, node_init_list_t *list) {
  int cursor = leaf_index_at_offset(&lowering->leaves, context_offset);
  if (cursor < 0) cursor = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    token_t *tok = entry->tok ? entry->tok : lowering->fallback_tok;
    psx_initializer_target_t target = entry->designator_count > 0
        ? psx_resolve_initializer_designator_path_with_records(
              diagnostics(lowering),
              ps_lowering_semantic_types(lowering->lowering_context),
              ps_lowering_record_decls(lowering->lowering_context),
              ps_lowering_record_layouts(lowering->lowering_context),
              ps_lowering_target(lowering->lowering_context), entry,
              ps_lowering_type_id(
                  lowering->lowering_context, context_type),
              context_offset, tok)
        : positional_target(
              lowering, context_type, context_offset,
              &lowering->leaves, cursor,
              entry->value && entry->value->kind == ND_INIT_LIST);
    if (!target.type) {
      ps_diag_ctx_in(
          diagnostics(lowering), tok, "static-init", "%s",
          diag_message_for_in(
              diagnostics(lowering),
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    mark_union_target(lowering, &target);

    if (entry->value && entry->value->kind == ND_INIT_LIST) {
      lower_array_list(
          lowering, target.type, target.relative_offset,
          (node_init_list_t *)entry->value);
    } else if (entry->value && entry->value->kind == ND_STRING &&
               (is_character_array_for_string(
                    lowering, target.type,
                    (node_string_t *)entry->value) ||
                (cursor >= 0 && cursor < lowering->leaves.count &&
                 is_character_array_for_string(
                     lowering,
                     lowering->leaves.items[cursor].string_array_type,
                     (node_string_t *)entry->value)))) {
      const psx_type_t *string_type = target.type;
      int string_offset = target.relative_offset;
      if (string_type->kind != PSX_TYPE_ARRAY && cursor >= 0 &&
          cursor < lowering->leaves.count) {
        psx_initializer_scalar_leaf_t *leaf = &lowering->leaves.items[cursor];
        if (leaf->string_array_type) {
          string_type = leaf->string_array_type;
          string_offset = leaf->string_array_offset;
          target.type = string_type;
          target.type_id = leaf->string_array_type_id;
          target.relative_offset = string_offset;
        }
      }
      write_string_value(
          lowering, string_type, string_offset,
          (node_string_t *)entry->value, tok);
    } else {
      write_scalar_value(lowering, &target, entry->value, tok);
    }
    cursor = psx_initializer_leaf_cursor_after_target_with_records(
        ps_lowering_semantic_types(lowering->lowering_context),
        ps_lowering_record_layouts(lowering->lowering_context),
        ps_lowering_target(lowering->lowering_context),
        &lowering->leaves, &target);
  }
}

static int type_contains_float(
    const psx_lowering_context_t *lowering_context,
    const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_FLOAT) return 1;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_FUNCTION)
    return 0;
  if (type->kind == PSX_TYPE_ARRAY)
    return type_contains_float(lowering_context, type->base);
  const psx_record_decl_t *record = record_decl(lowering_context, type);
  if (ps_type_is_tag_aggregate(type) && record) {
    for (int i = 0; i < record->member_count; i++) {
      if (type_contains_float(
              lowering_context,
              ps_tag_member_decl_type(&record->members[i]))) return 1;
    }
  }
  return 0;
}

int lower_static_object_initializer(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok) {
  if (!lowering_context || !global || !type || !initializer ||
      (type->kind != PSX_TYPE_ARRAY && !ps_type_is_tag_aggregate(type)))
    return 0;
  static_array_lowering_t lowering = {
      .lowering_context = lowering_context,
      .global = global,
      .fallback_tok = fallback_tok,
  };
  if (!psx_collect_initializer_scalar_leaves_with_records(
          ps_lowering_semantic_types(lowering_context),
          ps_lowering_record_decls(lowering_context),
          ps_lowering_record_layouts(lowering_context),
          ps_lowering_target(lowering_context),
          ps_gvar_decl_type_id(global), 0,
          &lowering.leaves) || lowering.leaves.count <= 0) {
    psx_initializer_scalar_leaf_list_dispose(&lowering.leaves);
    return 0;
  }
  ps_gvar_init_slots_alloc(
      global, lowering.leaves.count,
      type_contains_float(lowering_context, type));
  global->init_count = lowering.leaves.count;
  for (int i = 0; i < lowering.leaves.count; i++)
    ps_gvar_init_slot_clear(global, i);
  lower_array_list(&lowering, type, 0, initializer);
  psx_initializer_scalar_leaf_list_dispose(&lowering.leaves);
  return 1;
}

int lower_static_scalar_array_initializer(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  return lower_static_object_initializer(
      lowering_context, global, type, initializer, fallback_tok);
}

static int lower_static_string_expression(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type, node_string_t *string) {
  if (!global || !type || !string) return 0;
  if (type->kind == PSX_TYPE_POINTER) {
    global->init_symbol = string->string_label;
    global->init_symbol_len = -1;
    return 1;
  }
  if (type->kind != PSX_TYPE_ARRAY) return 0;

  const psx_type_t *element = ps_type_array_leaf_type(type);
  int element_size = lowering_type_size(lowering_context, element);
  int char_width = (int)string->char_width;
  if (char_width <= 0) char_width = 1;
  if (element_size != char_width) return 0;

  int total = string->byte_len + 1;
  ps_gvar_init_slots_alloc(global, total, 0);
  if (string->literal_contents) {
    ps_gvar_init_slots_write_string_units(
        global, 0, string->literal_contents, string->literal_length,
        element_size, string->byte_len);
  }
  ps_gvar_init_slot_write(global, string->byte_len, 0, 0.0, NULL, 0);
  global->init_count = total;
  return 1;
}

static int lower_static_scalar_expression(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type, node_t *initializer) {
  if (!global || !type || !initializer) return 0;
  if (initializer->kind == ND_STRING)
    return lower_static_string_expression(
        lowering_context, global, type, (node_string_t *)initializer);

  int integer_ok = 1;
  long long integer = psx_eval_const_int(initializer, &integer_ok);
  if (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX) {
    int floating_ok = 1;
    double floating = psx_eval_const_fp(initializer, &floating_ok);
    if (floating_ok) {
      global->fval = floating;
      return 1;
    }
  }
  if (integer_ok) {
    global->init_val = type->kind == PSX_TYPE_BOOL ? integer != 0 : integer;
    return 1;
  }

  char *symbol = NULL;
  int symbol_len = 0;
  long long offset = 0;
  if (psx_resolve_static_address_constant(
          initializer, &symbol, &symbol_len, &offset)) {
    global->init_symbol = symbol;
    global->init_symbol_len = symbol_len;
    global->init_symbol_offset = offset;
    return 1;
  }
  if (initializer->kind == ND_FUNCREF) {
    node_funcref_t *function = (node_funcref_t *)initializer;
    global->init_symbol = function->funcname;
    global->init_symbol_len = function->funcname_len;
    return 1;
  }
  return 0;
}

int lower_resolved_static_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *tok,
    psx_static_declaration_initializer_result_t *result) {
  if (result) *result = (psx_static_declaration_initializer_result_t){0};
  if (!global_registry || !lowering_context || !global || !resolution ||
      resolution->status != PSX_STATIC_INITIALIZER_OK ||
      !resolution->type || !resolution->initializer)
    return 0;

  const psx_type_t *type = resolution->type;
  if (resolution->type_completed) {
    if (!ps_global_registry_complete_array_type(
            global_registry, global, type)) return 0;
    if (result) result->type_completed = 1;
  }

  if (resolution->is_aggregate_initializer) {
    if (!lower_static_object_initializer(
            lowering_context, global, type,
            (node_init_list_t *)resolution->initializer, tok))
      return 0;
    global->has_init = 1;
    if (result) result->initialized = 1;
    return 1;
  }

  if (!lower_static_scalar_expression(
          lowering_context, global, type, resolution->initializer)) return 0;
  global->has_init = 1;
  if (result) result->initialized = 1;
  return 1;
}
