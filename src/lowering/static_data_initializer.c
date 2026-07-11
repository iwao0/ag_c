#include "static_data_initializer.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/literal_public.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_pass.h"
#include "../semantic/constant_expression.h"
#include "../semantic/initializer_resolution.h"
#include "../tokenizer/literals.h"

typedef struct {
  global_var_t *global;
  psx_initializer_scalar_leaf_list_t leaves;
  token_t *fallback_tok;
} static_array_lowering_t;

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
  if (target->direct_member) {
    for (int i = 0; i < leaves->count; i++) {
      if (leaves->items[i].relative_offset == target->relative_offset &&
          leaves->items[i].direct_member == target->direct_member)
        return i;
    }
  }
  return leaf_index_at_offset(leaves, target->relative_offset);
}

static psx_initializer_target_t positional_target(
    psx_type_t *context_type, int context_offset,
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
  psx_initializer_scalar_leaf_t *leaf = &leaves->items[cursor];
  target.type = leaf->type;
  target.relative_offset = leaf->relative_offset;
  target.direct_member = leaf->direct_member;
  if (context_type->kind == PSX_TYPE_UNION &&
      context_type->aggregate_definition &&
      context_type->aggregate_definition->member_count > 0) {
    tag_member_info_t *member =
        &context_type->aggregate_definition->members[0];
    if (preserve_subobject) {
      target.type = ps_tag_member_decl_type_mut(member);
      target.relative_offset = context_offset + member->offset;
      target.direct_member = member;
    }
    target.first_member_index = 0;
    target.union_relative_offset = context_offset;
    target.union_member_index = 0;
    return target;
  }
  if (!preserve_subobject) return target;
  if (context_type->kind == PSX_TYPE_STRUCT &&
      context_type->aggregate_definition) {
    psx_aggregate_definition_t *definition =
        context_type->aggregate_definition;
    for (int i = 0; i < definition->member_count; i++) {
      tag_member_info_t *member = &definition->members[i];
      psx_type_t *member_type = ps_tag_member_decl_type_mut(member);
      int member_offset = context_offset + member->offset;
      if (member_offset != leaf->relative_offset || !member_type) continue;
      if (member_type->kind != PSX_TYPE_ARRAY &&
          !ps_type_is_tag_aggregate(member_type)) continue;
      target.type = member_type;
      target.relative_offset = member_offset;
      target.direct_member = member;
      target.first_member_index = i;
      return target;
    }
    return target;
  }
  if (context_type->kind != PSX_TYPE_ARRAY || !context_type->base)
    return target;

  int child_size = ps_type_sizeof(context_type->base);
  if (child_size <= 0 || leaf->relative_offset < context_offset) return target;
  int child_index = (leaf->relative_offset - context_offset) / child_size;
  int child_offset = context_offset + child_index * child_size;
  if (child_index < 0 || child_index >= context_type->array_len ||
      child_offset != leaf->relative_offset) return target;
  target.type = context_type->base;
  target.relative_offset = child_offset;
  target.direct_member = NULL;
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
    psx_gvar_init_slot_set_ordinal(
        lowering->global, index, target->union_member_index);
}

static void write_scalar_value(
    static_array_lowering_t *lowering,
    const psx_initializer_target_t *target, node_t *value,
    token_t *tok) {
  int index = leaf_index_for_target(&lowering->leaves, target);
  if (index < 0 || index >= lowering->leaves.count) {
    psx_diag_ctx(tok ? tok : lowering->fallback_tok, "static-init", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  }
  psx_type_t *type = target->type;
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
  psx_gvar_init_slot_write(
      lowering->global, index, integer, floating, symbol, symbol_len);
  if (!symbol && type && type->kind == PSX_TYPE_FLOAT &&
      target->union_member_index >= 0) {
    psx_gvar_init_slot_write_fp_sentinel(
        lowering->global, index, type->fp_kind, ps_type_sizeof(type));
  }
}

static void write_string_value(
    static_array_lowering_t *lowering, psx_type_t *array_type,
    int relative_offset, node_string_t *string, token_t *tok) {
  psx_type_t *element = array_type;
  while (element && element->kind == PSX_TYPE_ARRAY) element = element->base;
  int element_size = ps_type_sizeof(element);
  int total_size = ps_type_sizeof(array_type);
  int capacity = element_size > 0 ? total_size / element_size : 0;
  int start = leaf_index_at_offset(&lowering->leaves, relative_offset);
  int char_width = (int)string->char_width;
  if (char_width <= 0) char_width = 1;
  if (!element || capacity <= 0 || start < 0 || element_size != char_width) {
    psx_diag_ctx(tok ? tok : lowering->fallback_tok, "static-init", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  }
  psx_string_lit_view_t literal = ps_string_lit_view(
      psx_find_string_lit_by_label(string->string_label));
  if (!literal.str) {
    psx_diag_ctx(tok ? tok : lowering->fallback_tok, "static-init", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }
  psx_gvar_init_slots_write_string_units(
      lowering->global, start, literal.str, literal.len,
      element_size, capacity);
}

static int is_character_array_for_string(
    psx_type_t *type, const node_string_t *string) {
  if (!type || type->kind != PSX_TYPE_ARRAY || !string) return 0;
  psx_type_t *element = type;
  while (element && element->kind == PSX_TYPE_ARRAY) element = element->base;
  if (!element || element->kind == PSX_TYPE_POINTER ||
      element->kind == PSX_TYPE_FUNCTION ||
      ps_type_is_tag_aggregate(element)) return 0;
  int width = (int)string->char_width;
  if (width <= 0) width = 1;
  return ps_type_sizeof(element) == width;
}

static void lower_array_list(
    static_array_lowering_t *lowering, psx_type_t *context_type,
    int context_offset, node_init_list_t *list) {
  int cursor = leaf_index_at_offset(&lowering->leaves, context_offset);
  if (cursor < 0) cursor = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    token_t *tok = entry->tok ? entry->tok : lowering->fallback_tok;
    psx_initializer_target_t target = entry->designator_count > 0
        ? psx_resolve_initializer_designator_path(
              entry, context_type, context_offset, tok)
        : positional_target(
              context_type, context_offset, &lowering->leaves, cursor,
              entry->value && entry->value->kind == ND_INIT_LIST);
    if (!target.type) {
      psx_diag_ctx(tok, "static-init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    mark_union_target(lowering, &target);

    if (entry->value && entry->value->kind == ND_INIT_LIST) {
      lower_array_list(
          lowering, target.type, target.relative_offset,
          (node_init_list_t *)entry->value);
    } else if (entry->value && entry->value->kind == ND_STRING &&
               (is_character_array_for_string(
                    target.type, (node_string_t *)entry->value) ||
                (cursor >= 0 && cursor < lowering->leaves.count &&
                 is_character_array_for_string(
                     lowering->leaves.items[cursor].string_array_type,
                     (node_string_t *)entry->value)))) {
      psx_type_t *string_type = target.type;
      int string_offset = target.relative_offset;
      if (string_type->kind != PSX_TYPE_ARRAY && cursor >= 0 &&
          cursor < lowering->leaves.count) {
        psx_initializer_scalar_leaf_t *leaf = &lowering->leaves.items[cursor];
        if (leaf->string_array_type) {
          string_type = leaf->string_array_type;
          string_offset = leaf->string_array_offset;
          target.type = string_type;
          target.relative_offset = string_offset;
        }
      }
      write_string_value(
          lowering, string_type, string_offset,
          (node_string_t *)entry->value, tok);
    } else {
      write_scalar_value(lowering, &target, entry->value, tok);
    }
    cursor = psx_initializer_leaf_cursor_after_target(
        &lowering->leaves, &target);
  }
}

static int type_contains_float(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_FLOAT) return 1;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_FUNCTION)
    return 0;
  if (type->kind == PSX_TYPE_ARRAY)
    return type_contains_float(type->base);
  if (ps_type_is_tag_aggregate(type) && type->aggregate_definition) {
    for (int i = 0; i < type->aggregate_definition->member_count; i++) {
      if (type_contains_float(ps_tag_member_decl_type(
              &type->aggregate_definition->members[i]))) return 1;
    }
  }
  return 0;
}

int lower_static_object_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok) {
  if (!global || !type || !initializer ||
      (type->kind != PSX_TYPE_ARRAY && !ps_type_is_tag_aggregate(type)))
    return 0;
  static_array_lowering_t lowering = {
      .global = global,
      .fallback_tok = fallback_tok,
  };
  if (!psx_collect_initializer_scalar_leaves(
          type, 0, &lowering.leaves) || lowering.leaves.count <= 0) {
    psx_initializer_scalar_leaf_list_dispose(&lowering.leaves);
    return 0;
  }
  psx_gvar_init_slots_alloc(
      global, lowering.leaves.count, type_contains_float(type));
  global->init_count = lowering.leaves.count;
  for (int i = 0; i < lowering.leaves.count; i++)
    psx_gvar_init_slot_clear(global, i);
  psx_semantic_analyze_initializer_syntax(
      (node_t *)initializer, fallback_tok);
  lower_array_list(&lowering, type, 0, initializer);
  psx_initializer_scalar_leaf_list_dispose(&lowering.leaves);
  return 1;
}

int lower_static_scalar_array_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  return lower_static_object_initializer(
      global, type, initializer, fallback_tok);
}
