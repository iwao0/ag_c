#include "initializer_lowering.h"

#include "../parser/lvar_public.h"
#include "../parser/config_runtime.h"
#include "../parser/node_utils.h"
#include "../parser/literal_public.h"
#include "../parser/symtab.h"
#include "../parser/diag.h"
#include "../parser/tag_public.h"
#include "../diag/diag.h"
#include "../semantic/constant_expression.h"
#include "../semantic/initializer_resolution.h"
#include "../tokenizer/literals.h"
#include <stdlib.h>
#include <string.h>

static node_t *append_init(node_t *chain, node_t *item) {
  return chain ? ps_node_new_binary(ND_COMMA, chain, item) : item;
}

static int initializer_value_is_zero(const node_t *value) {
  if (!value || value->kind != ND_NUM) return 0;
  const node_num_t *number = (const node_num_t *)value;
  return ps_node_value_fp_kind((node_t *)value) != TK_FLOAT_KIND_NONE
             ? number->fval == 0.0
             : number->val == 0;
}

int psx_initializer_lowering_supports_recursive_aggregate(
    const psx_type_t *type) {
  type = ps_type_array_leaf_type(type);
  if (!type || !ps_type_is_tag_aggregate(type)) return 0;
  const psx_aggregate_definition_t *definition = type->aggregate_definition;
  if (!definition) return 0;
  for (int i = 0; i < definition->member_count; i++) {
    const tag_member_info_t *member = &definition->members[i];
    if (member->len <= 0) {
      const psx_type_t *member_type = ps_tag_member_decl_type(member);
      if (!member_type || !ps_type_is_tag_aggregate(member_type) ||
          !psx_initializer_lowering_supports_recursive_aggregate(member_type))
        return 0;
      continue;
    }
    const psx_type_t *member_type = ps_tag_member_decl_type(member);
    const psx_type_t *leaf = ps_type_array_leaf_type(member_type);
    if (leaf && ps_type_is_tag_aggregate(leaf) &&
        !psx_initializer_lowering_supports_recursive_aggregate(member_type))
      return 0;
  }
  return 1;
}

static node_t *new_array_elem_assign(lvar_t *var, int idx, node_t *value) {
  return ps_node_new_assign(ps_node_new_array_elem_lvar_for(var, idx), value);
}

typedef struct {
  lvar_t *var;
  node_t *chain;
  int idx;
  int limit;
  unsigned char *assigned;
} array_string_lowering_ctx_t;

static void append_array_string_unit(uint32_t unit, void *user) {
  array_string_lowering_ctx_t *ctx = user;
  if (ctx->idx >= ctx->limit) return;
  ctx->chain = append_init(
      ctx->chain,
      new_array_elem_assign(ctx->var, ctx->idx, ps_node_new_num(unit)));
  if (ctx->assigned) ctx->assigned[ctx->idx] = 1;
  ctx->idx++;
}

static node_t *lower_array_string_initializer_at(
    lvar_t *var, node_string_t *string, int base_index, int capacity,
    int array_len, unsigned char *assigned, node_t *chain, token_t *tok) {
  psx_string_lit_view_t literal = ps_string_lit_view(
      ps_find_string_lit_by_label(string->string_label));
  if (!literal.str) {
    ps_diag_ctx(tok, "init", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }

  int elem_size = ps_lvar_array_scalar_element_size(var);
  if (elem_size <= 0) elem_size = 1;
  int limit = base_index + capacity;
  if (limit > array_len) limit = array_len;
  array_string_lowering_ctx_t ctx = {
      var, chain, base_index, limit, assigned};
  tk_emit_string_code_units(literal.str, literal.len, elem_size, capacity,
                            append_array_string_unit, &ctx);
  while (ctx.idx < limit) {
    ctx.chain = append_init(
        ctx.chain,
        new_array_elem_assign(var, ctx.idx, ps_node_new_num(0)));
    if (assigned) assigned[ctx.idx] = 1;
    ctx.idx++;
  }
  return ctx.chain ? ctx.chain : ps_node_new_num(0);
}

static node_t *lower_array_string_initializer(lvar_t *var, node_string_t *string,
                                              int array_len, token_t *tok) {
  return lower_array_string_initializer_at(
      var, string, 0, array_len, array_len, NULL, NULL, tok);
}

static node_t *lower_array_expr_initializer(node_t *target, node_t *value,
                                            token_t *tok) {
  lvar_t *var = ps_node_lvar_symbol(target);
  if (!var) return NULL;
  int elem_size = ps_lvar_array_scalar_element_size(var);
  int array_len = ps_lvar_array_flat_element_count(var);
  if (array_len <= 0 && elem_size > 0) {
    array_len = ps_lvar_storage_size(var, 0) / elem_size;
  }

  if (value->kind == ND_STRING) {
    int char_width = (int)((node_string_t *)value)->char_width;
    if (char_width <= 0) char_width = 1;
    if (elem_size == char_width) {
      return lower_array_string_initializer(
          var, (node_string_t *)value, array_len, tok);
    }
  }

  if (array_len > 0 && value->kind == ND_COMMA && value->rhs &&
      value->rhs->kind == ND_ADDR && value->rhs->lhs &&
      value->rhs->lhs->kind == ND_LVAR) {
    node_lvar_t *source = (node_lvar_t *)value->rhs->lhs;
    node_t *chain = value->lhs;
    for (int i = 0; i < array_len; i++) {
      node_t *dst = ps_node_new_array_elem_lvar_for(var, i);
      node_t *src = ps_node_new_lvar_typed_at_for(
          source->var, source->offset + i * elem_size, elem_size);
      chain = append_init(chain, ps_node_new_assign(dst, src));
    }
    return chain;
  }

  if (array_len > 0) {
    node_t *chain = new_array_elem_assign(var, 0, value);
    for (int i = 1; i < array_len; i++) {
      chain = append_init(
          chain, new_array_elem_assign(var, i, ps_node_new_num(0)));
    }
    return chain;
  }
  return ps_node_new_num(0);
}

static int resolve_initializer_entry_index(
    const psx_initializer_entry_t *entry, token_t *fallback_tok,
    lvar_t *var, int depth, int implicit_index, int base_index) {
  if (entry->index_expr_count == 0 && !entry->has_index) return implicit_index;
  if (entry->index_expr_count == 0) {
    int stride = ps_lvar_array_designator_stride_elements(var, depth);
    if (stride <= 0) stride = 1;
    return base_index + (int)entry->index * stride;
  }
  int index = base_index;
  for (int d = 0; d < entry->index_expr_count; d++) {
    int ok = 1;
    long long resolved = psx_eval_const_int(entry->index_exprs[d], &ok);
    if (!ok) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init",
                   diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                   diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
    }
    if (resolved < 0) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init",
                   diag_message_for(DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                   diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
    }
    int stride = ps_lvar_array_designator_stride_elements(var, depth + d);
    if (stride <= 0) stride = 1;
    index += (int)resolved * stride;
  }
  return index;
}

static int entry_initializes_character_subarray(
    lvar_t *var, const node_t *value, int stride) {
  if (!var || !value || value->kind != ND_STRING || stride <= 1) return 0;
  int elem_size = ps_lvar_array_scalar_element_size(var);
  int char_width = (int)((const node_string_t *)value)->char_width;
  if (char_width <= 0) char_width = 1;
  return elem_size == char_width;
}

static void lower_array_initializer_entries(
    lvar_t *var, node_init_list_t *list, int depth, int base_index,
    int array_len, unsigned char *assigned, node_t **chain,
    token_t *fallback_tok) {
  int stride = ps_lvar_array_designator_stride_elements(var, depth);
  if (stride <= 0) stride = 1;
  int next_index = base_index;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    int index = resolve_initializer_entry_index(
        entry, fallback_tok, var, depth, next_index, base_index);
    if (index < 0 || index >= array_len) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    if (entry->value && entry->value->kind == ND_INIT_LIST) {
      lower_array_initializer_entries(
          var, (node_init_list_t *)entry->value, depth + 1, index,
          array_len, assigned, chain, fallback_tok);
      next_index = index + stride;
    } else if (entry_initializes_character_subarray(
                   var, entry->value, stride)) {
      if (index + stride > array_len) {
        ps_diag_ctx(entry->tok ? entry->tok : fallback_tok,
                     "init", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      *chain = lower_array_string_initializer_at(
          var, (node_string_t *)entry->value, index, stride,
          array_len, assigned, *chain,
          entry->tok ? entry->tok : fallback_tok);
      next_index = index + stride;
    } else {
      *chain = append_init(
          *chain, new_array_elem_assign(var, index, entry->value));
      assigned[index] = 1;
      next_index = index + 1;
    }
  }
}

static node_t *lower_typed_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok);
static node_t *append_object_zero_fill(lvar_t *var, node_t *chain);
static node_t *append_typed_object_zero_fill(
    lvar_t *var, const psx_type_t *type, node_t *chain);
static node_t *try_lower_typed_array_copy(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_t *value, node_t *chain);

typedef struct {
  lvar_t *var;
  const psx_type_t *element_type;
  node_t *chain;
  int relative_offset;
  int element_size;
  int index;
  int capacity;
} typed_string_lowering_ctx_t;

static void append_typed_string_unit(uint32_t unit, void *user) {
  typed_string_lowering_ctx_t *ctx = user;
  if (ctx->index >= ctx->capacity) return;
  node_t *target = ps_node_new_lvar_type_at_for(
      ctx->var,
      ps_lvar_offset(ctx->var) + ctx->relative_offset +
          ctx->index * ctx->element_size,
      ctx->element_type);
  ctx->chain = append_init(
      ctx->chain, ps_node_new_assign(target, ps_node_new_num(unit)));
  ctx->index++;
}

static node_t *lower_typed_character_array_string(
    lvar_t *var, const psx_type_t *array_type, int relative_offset,
    node_string_t *string, node_t *chain, token_t *tok) {
  const psx_type_t *element = ps_type_array_leaf_type(array_type);
  int element_size = ps_type_sizeof(element);
  int capacity = element_size > 0
                     ? ps_type_sizeof(array_type) / element_size
                     : 0;
  int char_width = (int)string->char_width;
  if (char_width <= 0) char_width = 1;
  if (!element || capacity <= 0 || element_size != char_width) return NULL;

  psx_string_lit_view_t literal = ps_string_lit_view(
      ps_find_string_lit_by_label(string->string_label));
  if (!literal.str) {
    ps_diag_ctx(tok, "init", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }
  typed_string_lowering_ctx_t ctx = {
      var, element, chain, relative_offset, element_size, 0, capacity};
  tk_emit_string_code_units(literal.str, literal.len, element_size, capacity,
                            append_typed_string_unit, &ctx);
  while (ctx.index < capacity)
    append_typed_string_unit(0, &ctx);
  return ctx.chain;
}

static int type_has_aggregate_leaf(const psx_type_t *type) {
  return ps_type_is_tag_aggregate(ps_type_array_leaf_type(type));
}

static node_t *lower_array_list_initializer(node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  if (!var || !initializer->base.rhs ||
      initializer->base.rhs->kind != ND_INIT_LIST) return NULL;
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  const psx_type_t *type = ps_lvar_get_decl_type(var);
  if (type && type->kind == PSX_TYPE_ARRAY && list->entry_count == 1) {
    psx_initializer_entry_t *entry = &list->entries[0];
    if (entry->value && entry->value->kind == ND_STRING &&
        entry->designator_count == 0 && !entry->has_index &&
        !entry->has_member) {
      node_t *lowered = lower_typed_character_array_string(
          var, type, 0, (node_string_t *)entry->value, NULL,
          entry->tok ? entry->tok : initializer->base.tok);
      if (lowered) return lowered;
    }
  }
  if (type_has_aggregate_leaf(type)) {
    node_t *chain = append_typed_object_zero_fill(var, type, NULL);
    return lower_typed_initializer_list(
        var, type, 0, list, chain, initializer->base.tok);
  }
  int array_len = ps_lvar_array_flat_element_count(var);
  int elem_size = ps_lvar_array_scalar_element_size(var);
  if (array_len <= 0 && elem_size > 0) {
    array_len = ps_lvar_storage_size(var, 0) / elem_size;
  }

  unsigned char *assigned = calloc(
      (size_t)(array_len > 0 ? array_len : 1), 1);
  if (!assigned) return NULL;
  node_t *chain = NULL;
  lower_array_initializer_entries(
      var, list, 0, 0, array_len, assigned, &chain,
      initializer->base.tok);
  for (int i = 0; i < array_len; i++) {
    if (!assigned[i]) {
      chain = append_init(
          chain, new_array_elem_assign(var, i, ps_node_new_num(0)));
    }
  }
  free(assigned);
  return chain ? chain : ps_node_new_num(0);
}

static node_t *append_object_zero_fill(lvar_t *var, node_t *chain) {
  int size = ps_lvar_decl_sizeof(var, ps_lvar_storage_size(var, 0));
  int offset = 0;
  const int widths[] = {8, 4, 2, 1};
  for (int w = 0; w < 4; w++) {
    int width = widths[w];
    while (offset + width <= size) {
      node_t *slot = ps_node_new_lvar_typed_at_for(
          var, ps_lvar_offset(var) + offset, width);
      chain = append_init(
          chain, ps_node_new_assign(slot, ps_node_new_num(0)));
      offset += width;
    }
  }
  return chain;
}

static int aggregate_member_index(
    const psx_aggregate_definition_t *definition,
    const psx_initializer_entry_t *entry, int positional_index) {
  if (!entry->has_member) {
    int covered_end = -1;
    for (int i = 0; i < definition->member_count; i++) {
      const tag_member_info_t *member = &definition->members[i];
      int member_size = ps_tag_member_decl_storage_size(member);
      if (member->len <= 0 && member_size > 0) {
        if (i >= positional_index) return i;
        int end = member->offset + member_size;
        if (end > covered_end) covered_end = end;
        continue;
      }
      if (member->offset < covered_end) continue;
      if (i >= positional_index) return i;
    }
    return -1;
  }
  for (int i = 0; i < definition->member_count; i++) {
    const tag_member_info_t *member = &definition->members[i];
    if (member->len == entry->member_len && member->name &&
        memcmp(member->name, entry->member_name,
               (size_t)entry->member_len) == 0) return i;
  }
  return -1;
}

static int aggregate_ordinal_after_member(
    const psx_aggregate_definition_t *definition, int member_index) {
  if (!definition || member_index < 0 ||
      member_index >= definition->member_count) return member_index + 1;
  const tag_member_info_t *selected = &definition->members[member_index];
  for (int i = 0; i < definition->member_count; i++) {
    const tag_member_info_t *container = &definition->members[i];
    if (container->len > 0) continue;
    int size = ps_tag_member_decl_storage_size(container);
    if (size <= 0) continue;
    int start = container->offset;
    int end = start + size;
    if (selected->offset < start || selected->offset >= end) continue;
    int next = i + 1;
    while (next < definition->member_count &&
           definition->members[next].offset < end)
      next++;
    return next;
  }
  return member_index + 1;
}

static long long resolve_typed_designator_index(
    const psx_initializer_entry_t *entry, int index_pos,
    token_t *fallback_tok) {
  int ok = 1;
  long long index = psx_eval_const_int(entry->index_exprs[index_pos], &ok);
  if (!ok) {
    ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init",
                 diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                 diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  if (index < 0) {
    ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init",
                 diag_message_for(DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                 diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  return index;
}

typedef psx_initializer_target_t typed_designator_target_t;

static int descend_array_designators(
    const psx_initializer_entry_t *entry, int first_index_pos,
    const psx_type_t **type_inout, int *relative_offset_inout,
    token_t *fallback_tok) {
  int first_index = -1;
  for (int d = first_index_pos; d < entry->index_expr_count; d++) {
    const psx_type_t *array = *type_inout;
    if (!array || array->kind != PSX_TYPE_ARRAY || !array->base) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    long long index = resolve_typed_designator_index(entry, d, fallback_tok);
    if (index >= array->array_len) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    if (first_index < 0) first_index = (int)index;
    int child_size = ps_type_sizeof(array->base);
    *relative_offset_inout += (int)index * child_size;
    *type_inout = array->base;
  }
  return first_index;
}

static node_t *lower_typed_initializer_value(
    lvar_t *var, const psx_type_t *type, int relative_offset, node_t *value,
    const tag_member_info_t *direct_member, node_t *chain,
    token_t *fallback_tok) {
  if (value && value->kind == ND_INIT_LIST) {
    return lower_typed_initializer_list(
        var, type, relative_offset, (node_init_list_t *)value,
        chain, fallback_tok);
  }
  if (value && value->kind == ND_STRING && type &&
      type->kind == PSX_TYPE_ARRAY) {
    node_t *lowered = lower_typed_character_array_string(
        var, type, relative_offset, (node_string_t *)value,
        chain, fallback_tok);
    if (lowered) return lowered;
  }
  if (value && type && type->kind == PSX_TYPE_ARRAY) {
    node_t *copied = try_lower_typed_array_copy(
        var, type, relative_offset, value, chain);
    if (copied) return copied;
  }
  if (initializer_value_is_zero(value) && type &&
      (type->kind == PSX_TYPE_ARRAY || ps_type_is_tag_aggregate(type))) {
    return chain;
  }
  node_t *target = direct_member
                       ? ps_node_new_tag_member_lvar_ref_for(
                             var, relative_offset, direct_member)
                       : ps_node_new_lvar_type_at_for(
                             var, ps_lvar_offset(var) + relative_offset, type);
  return append_init(chain, ps_node_new_assign(target, value));
}

static int initializer_list_is_flat_positional_scalar(
    const node_init_list_t *list) {
  if (!list || list->entry_count <= 0) return 0;
  for (int i = 0; i < list->entry_count; i++) {
    const psx_initializer_entry_t *entry = &list->entries[i];
    const psx_type_t *value_type = ps_node_get_type(entry->value);
    const psx_type_t *addressed_type =
        entry->value && entry->value->kind == ND_ADDR
            ? ps_node_get_type(entry->value->lhs)
            : NULL;
    if (entry->designator_count > 0 || entry->has_index ||
        entry->has_member || !entry->value ||
        entry->value->kind == ND_INIT_LIST ||
        ps_type_is_tag_aggregate(value_type) ||
        (addressed_type && addressed_type->kind == PSX_TYPE_ARRAY)) return 0;
  }
  return 1;
}

static int initializer_list_has_no_nested_values(
    const node_init_list_t *list) {
  if (!list || list->entry_count <= 0) return 0;
  for (int i = 0; i < list->entry_count; i++) {
    if (!list->entries[i].value ||
        list->entries[i].value->kind == ND_INIT_LIST) return 0;
  }
  return 1;
}

typedef psx_initializer_scalar_leaf_t typed_scalar_leaf_t;
typedef psx_initializer_scalar_leaf_list_t typed_scalar_leaf_list_t;

static node_t *append_typed_object_zero_fill(
    lvar_t *var, const psx_type_t *type, node_t *chain) {
  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves(type, 0, &leaves)) {
    free(leaves.items);
    return append_object_zero_fill(var, chain);
  }
  for (int i = 0; i < leaves.count; i++) {
    const typed_scalar_leaf_t *leaf = &leaves.items[i];
    node_t *target = leaf->direct_member
                         ? ps_node_new_tag_member_lvar_ref_for(
                               var, leaf->relative_offset,
                               leaf->direct_member)
                         : ps_node_new_lvar_type_at_for(
                               var, ps_lvar_offset(var) + leaf->relative_offset,
                               leaf->type);
    chain = append_init(
        chain, ps_node_new_assign(target, ps_node_new_num(0)));
  }
  free(leaves.items);
  return chain;
}

static node_t *lower_flat_typed_object_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves(
          type, relative_offset, &leaves)) {
    free(leaves.items);
    ps_diag_ctx(fallback_tok, "init", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
  }
  int leaf_index = 0;
  for (int i = 0; i < list->entry_count; i++) {
    if (leaf_index >= leaves.count) {
      free(leaves.items);
      ps_diag_ctx(list->entries[i].tok ? list->entries[i].tok : fallback_tok,
                   "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const typed_scalar_leaf_t *leaf = &leaves.items[leaf_index];
    if (list->entries[i].value->kind == ND_STRING &&
        leaf->string_array_type) {
      node_t *lowered = lower_typed_character_array_string(
          var, leaf->string_array_type, leaf->string_array_offset,
          (node_string_t *)list->entries[i].value, chain,
          list->entries[i].tok ? list->entries[i].tok : fallback_tok);
      if (lowered) {
        chain = lowered;
        int row_offset = leaf->string_array_offset;
        do {
          leaf_index++;
        } while (leaf_index < leaves.count &&
                 leaves.items[leaf_index].string_array_type ==
                     leaf->string_array_type &&
                 leaves.items[leaf_index].string_array_offset == row_offset);
        continue;
      }
    }
    chain = lower_typed_initializer_value(
        var, leaf->type, leaf->relative_offset,
        list->entries[i].value, leaf->direct_member,
        chain, list->entries[i].tok ? list->entries[i].tok : fallback_tok);
    leaf_index++;
  }
  free(leaves.items);
  return chain;
}

static int initializer_value_requires_immediate_subobject(
    node_t *value) {
  const psx_type_t *value_type = ps_node_get_type(value);
  if (ps_type_is_tag_aggregate(value_type)) return 1;
  if (value && value->kind == ND_ADDR && value->lhs) {
    const psx_type_t *addressed = ps_node_get_type(value->lhs);
    return addressed && addressed->kind == PSX_TYPE_ARRAY;
  }
  return 0;
}

static int immediate_subobject_at_leaf_cursor(
    const psx_type_t *type, int relative_offset,
    const typed_scalar_leaf_list_t *leaves, int leaf_cursor,
    typed_designator_target_t *out) {
  if (!type || !leaves || leaf_cursor < 0 || leaf_cursor >= leaves->count ||
      !out) return 0;
  int leaf_offset = leaves->items[leaf_cursor].relative_offset;
  if (type->kind == PSX_TYPE_ARRAY && type->base) {
    int child_size = ps_type_sizeof(type->base);
    if (child_size <= 0 || leaf_offset < relative_offset) return 0;
    int child_index = (leaf_offset - relative_offset) / child_size;
    int child_offset = relative_offset + child_index * child_size;
    if (child_index < 0 || child_index >= type->array_len ||
        child_offset != leaf_offset) return 0;
    *out = (typed_designator_target_t){
        .type = type->base,
        .relative_offset = child_offset,
        .first_array_index = child_index,
        .first_member_index = -1,
    };
    return type->base->kind == PSX_TYPE_ARRAY ||
           ps_type_is_tag_aggregate(type->base);
  }
  if (type->kind == PSX_TYPE_STRUCT && type->aggregate_definition) {
    const psx_aggregate_definition_t *definition = type->aggregate_definition;
    for (int i = 0; i < definition->member_count; i++) {
      const tag_member_info_t *member = &definition->members[i];
      const psx_type_t *member_type = ps_tag_member_decl_type(member);
      int member_offset = relative_offset + member->offset;
      if (member_offset != leaf_offset || !member_type) continue;
      if (member_type->kind != PSX_TYPE_ARRAY &&
          !ps_type_is_tag_aggregate(member_type)) return 0;
      *out = (typed_designator_target_t){
          .type = member_type,
          .relative_offset = member_offset,
          .direct_member = member,
          .first_array_index = -1,
          .first_member_index = i,
      };
      return 1;
    }
  }
  return 0;
}

static node_t *lower_mixed_typed_object_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves(
          type, relative_offset, &leaves)) {
    free(leaves.items);
    return chain;
  }
  int leaf_cursor = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->designator_count > 0) {
      typed_designator_target_t target =
          psx_resolve_initializer_designator_path(
          entry, type, relative_offset, fallback_tok);
      chain = lower_typed_initializer_value(
          var, target.type, target.relative_offset, entry->value,
          target.direct_member, chain,
          entry->tok ? entry->tok : fallback_tok);
      leaf_cursor = psx_initializer_leaf_cursor_after_target(
          &leaves, &target);
      continue;
    }
    if (leaf_cursor >= leaves.count) {
      free(leaves.items);
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const typed_scalar_leaf_t *leaf = &leaves.items[leaf_cursor];
    if (initializer_value_requires_immediate_subobject(entry->value)) {
      typed_designator_target_t target = {0};
      if (immediate_subobject_at_leaf_cursor(
              type, relative_offset, &leaves, leaf_cursor, &target)) {
        chain = lower_typed_initializer_value(
            var, target.type, target.relative_offset, entry->value,
            target.direct_member, chain,
            entry->tok ? entry->tok : fallback_tok);
        leaf_cursor = psx_initializer_leaf_cursor_after_target(
            &leaves, &target);
        continue;
      }
    }
    if (entry->value->kind == ND_STRING && leaf->string_array_type) {
      node_t *lowered = lower_typed_character_array_string(
          var, leaf->string_array_type, leaf->string_array_offset,
          (node_string_t *)entry->value, chain,
          entry->tok ? entry->tok : fallback_tok);
      if (lowered) {
        chain = lowered;
        int row_offset = leaf->string_array_offset;
        do {
          leaf_cursor++;
        } while (leaf_cursor < leaves.count &&
                 leaves.items[leaf_cursor].string_array_type ==
                     leaf->string_array_type &&
                 leaves.items[leaf_cursor].string_array_offset == row_offset);
        continue;
      }
    }
    chain = lower_typed_initializer_value(
        var, leaf->type, leaf->relative_offset, entry->value,
        leaf->direct_member, chain,
        entry->tok ? entry->tok : fallback_tok);
    leaf_cursor++;
  }
  free(leaves.items);
  return chain;
}

static node_t *try_lower_typed_array_copy(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_t *value, node_t *chain) {
  if (!var || !type || type->kind != PSX_TYPE_ARRAY || !value ||
      value->kind != ND_ADDR || !value->lhs ||
      value->lhs->kind != ND_LVAR) return NULL;
  node_lvar_t *source = (node_lvar_t *)value->lhs;
  const psx_type_t *source_type = ps_node_get_type(value->lhs);
  if (!source->var || !source_type ||
      ps_type_sizeof(source_type) < ps_type_sizeof(type)) return NULL;

  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves(
          type, relative_offset, &leaves)) {
    free(leaves.items);
    return NULL;
  }
  for (int i = 0; i < leaves.count; i++) {
    const typed_scalar_leaf_t *leaf = &leaves.items[i];
    int source_offset = source->offset +
                        (leaf->relative_offset - relative_offset);
    node_t *src = ps_node_new_lvar_type_at_for(
        source->var, source_offset, leaf->type);
    node_t *dst = leaf->direct_member
                      ? ps_node_new_tag_member_lvar_ref_for(
                            var, leaf->relative_offset, leaf->direct_member)
                      : ps_node_new_lvar_type_at_for(
                            var, ps_lvar_offset(var) + leaf->relative_offset,
                            leaf->type);
    chain = append_init(chain, ps_node_new_assign(dst, src));
  }
  free(leaves.items);
  return chain;
}

static node_t *lower_flat_typed_array_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  const psx_type_t *leaf = ps_type_array_leaf_type(type);
  int leaf_size = ps_type_sizeof(leaf);
  int flat_count = ps_type_array_flat_element_count(type);
  if (!leaf || flat_count <= 0 || list->entry_count > flat_count) {
    ps_diag_ctx(fallback_tok, "init", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  }
  for (int i = 0; i < list->entry_count; i++) {
    node_t *target = ps_node_new_lvar_type_at_for(
        var, ps_lvar_offset(var) + relative_offset + i * leaf_size, leaf);
    chain = append_init(
        chain, ps_node_new_assign(target, list->entries[i].value));
  }
  return chain;
}

static node_t *lower_typed_array_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  if (list->entry_count == 1 &&
      initializer_value_is_zero(list->entries[0].value) &&
      list->entries[0].designator_count == 0) return chain;
  if (type_has_aggregate_leaf(type) &&
      initializer_list_has_no_nested_values(list)) {
    return lower_mixed_typed_object_initializer_list(
        var, type, relative_offset, list, chain, fallback_tok);
  }
  if (type->base && initializer_list_is_flat_positional_scalar(list) &&
      (type->base->kind == PSX_TYPE_ARRAY ||
       ps_type_is_tag_aggregate(type->base))) {
    int contains_string = 0;
    for (int i = 0; i < list->entry_count; i++) {
      if (list->entries[i].value &&
          list->entries[i].value->kind == ND_STRING) {
        contains_string = 1;
        break;
      }
    }
    if (ps_type_is_tag_aggregate(type->base) || contains_string) {
      return lower_flat_typed_object_initializer_list(
          var, type, relative_offset, list, chain, fallback_tok);
    }
    return lower_flat_typed_array_initializer_list(
        var, type, relative_offset, list, chain, fallback_tok);
  }
  int next_index = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->has_member && entry->designator_count == 0) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    const psx_type_t *target_type = type->base;
    int target_offset = relative_offset;
    int selected_index = next_index;
    if (entry->designator_count > 0) {
      if (entry->designators[0].kind != PSX_INIT_DESIGNATOR_INDEX) {
        ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      typed_designator_target_t target =
          psx_resolve_initializer_designator_path(
          entry, type, relative_offset, fallback_tok);
      target_type = target.type;
      target_offset = target.relative_offset;
      selected_index = target.first_array_index;
      chain = lower_typed_initializer_value(
          var, target_type, target_offset, entry->value,
          target.direct_member, chain,
          entry->tok ? entry->tok : fallback_tok);
      next_index = selected_index + 1;
      continue;
    } else if (entry->index_expr_count > 0) {
      target_type = type;
      selected_index = descend_array_designators(
          entry, 0, &target_type, &target_offset, fallback_tok);
    } else {
      if (selected_index < 0 || selected_index >= type->array_len) {
        ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      target_offset += selected_index * ps_type_sizeof(target_type);
    }
    chain = lower_typed_initializer_value(
        var, target_type, target_offset, entry->value, NULL,
        chain, entry->tok ? entry->tok : fallback_tok);
    next_index = selected_index + 1;
  }
  return chain;
}

static node_t *lower_typed_aggregate_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  const psx_aggregate_definition_t *definition = type->aggregate_definition;
  if (!definition) return chain;
  if (type->kind == PSX_TYPE_UNION && definition->member_count > 0 &&
      initializer_list_has_no_nested_values(list)) {
    int has_designator = 0;
    for (int i = 0; i < list->entry_count; i++) {
      if (list->entries[i].designator_count > 0 ||
          list->entries[i].has_member || list->entries[i].has_index) {
        has_designator = 1;
        break;
      }
    }
    const tag_member_info_t *first = &definition->members[0];
    const psx_type_t *first_type = ps_tag_member_decl_type(first);
    if (!has_designator && first_type &&
        first_type->kind == PSX_TYPE_ARRAY) {
      if (!ps_get_enable_union_array_member_nonbrace_init()) {
        ps_diag_ctx(fallback_tok, "decl", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED));
      }
      return lower_typed_array_initializer_list(
          var, first_type, relative_offset + first->offset,
          list, chain, fallback_tok);
    }
  }
  if (list->entry_count == 1 &&
      initializer_value_is_zero(list->entries[0].value) &&
      list->entries[0].designator_count == 0) return chain;
  if (type->kind == PSX_TYPE_STRUCT &&
      initializer_list_has_no_nested_values(list)) {
    return lower_mixed_typed_object_initializer_list(
        var, type, relative_offset, list, chain, fallback_tok);
  }
  if (type->kind == PSX_TYPE_STRUCT &&
      initializer_list_is_flat_positional_scalar(list)) {
    return lower_flat_typed_object_initializer_list(
        var, type, relative_offset, list, chain, fallback_tok);
  }
  int ordinal = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (type->kind == PSX_TYPE_UNION && i > 0 &&
        entry->designator_count == 0 && !entry->has_member) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
    }
    if (entry->designator_count > 0) {
      if (entry->designators[0].kind != PSX_INIT_DESIGNATOR_MEMBER) {
        ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      typed_designator_target_t target =
          psx_resolve_initializer_designator_path(
          entry, type, relative_offset, fallback_tok);
      chain = lower_typed_initializer_value(
          var, target.type, target.relative_offset, entry->value,
          target.direct_member, chain,
          entry->tok ? entry->tok : fallback_tok);
      if (type->kind == PSX_TYPE_STRUCT)
        ordinal = aggregate_ordinal_after_member(
            definition, target.first_member_index);
      continue;
    }
    int member_index = aggregate_member_index(definition, entry, ordinal);
    if (member_index < 0 || member_index >= definition->member_count) {
      ps_diag_ctx(entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for(
                       type->kind == PSX_TYPE_UNION
                           ? DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND
                           : DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const tag_member_info_t *member = &definition->members[member_index];
    const psx_type_t *target_type = ps_tag_member_decl_type(member);
    int target_offset = relative_offset + member->offset;
    const tag_member_info_t *direct_member = member;
    if (entry->index_expr_count > 0) {
      descend_array_designators(
          entry, 0, &target_type, &target_offset, fallback_tok);
      direct_member = NULL;
    }
    chain = lower_typed_initializer_value(
        var, target_type, target_offset, entry->value, direct_member,
        chain, entry->tok ? entry->tok : fallback_tok);
    if (type->kind == PSX_TYPE_STRUCT)
      ordinal = aggregate_ordinal_after_member(definition, member_index);
  }
  return chain;
}

static node_t *lower_typed_initializer_list(
    lvar_t *var, const psx_type_t *type, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  if (!type || !list) return chain;
  if (type->kind == PSX_TYPE_ARRAY) {
    return lower_typed_array_initializer_list(
        var, type, relative_offset, list, chain, fallback_tok);
  }
  if (ps_type_is_tag_aggregate(type)) {
    return lower_typed_aggregate_initializer_list(
        var, type, relative_offset, list, chain, fallback_tok);
  }
  if (list->entry_count == 1) {
    return lower_typed_initializer_value(
        var, type, relative_offset, list->entries[0].value, NULL,
        chain, fallback_tok);
  }
  ps_diag_ctx(fallback_tok, "init", "%s",
               diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  return chain;
}

static node_t *lower_struct_list_initializer(node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  psx_aggregate_definition_t *definition =
      type ? type->aggregate_definition : NULL;
  if (!var || !definition || !initializer->base.rhs ||
      initializer->base.rhs->kind != ND_INIT_LIST) return NULL;
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (psx_initializer_lowering_supports_recursive_aggregate(type)) {
    node_t *chain = append_typed_object_zero_fill(var, type, NULL);
    return lower_typed_initializer_list(
        var, type, 0, list, chain, initializer->base.tok);
  }
  node_t *chain = append_object_zero_fill(var, NULL);
  int ordinal = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    int member_index = aggregate_member_index(definition, entry, ordinal);
    if (member_index < 0 || member_index >= definition->member_count) {
      ps_diag_ctx(entry->tok ? entry->tok : initializer->base.tok,
                   "init", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    tag_member_info_t *member = &definition->members[member_index];
    node_t *lhs = ps_node_new_tag_member_lvar_ref_for(
        var, member->offset, member);
    chain = append_init(
        chain, ps_node_new_assign(lhs, entry->value));
    ordinal = aggregate_ordinal_after_member(definition, member_index);
  }
  return chain ? chain : ps_node_new_num(0);
}

static node_t *lower_union_list_initializer(node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  psx_aggregate_definition_t *definition =
      type ? type->aggregate_definition : NULL;
  if (!var || !definition || !initializer->base.rhs ||
      initializer->base.rhs->kind != ND_INIT_LIST) return NULL;
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (psx_initializer_lowering_supports_recursive_aggregate(type)) {
    node_t *chain = append_typed_object_zero_fill(var, type, NULL);
    return lower_typed_initializer_list(
        var, type, 0, list, chain, initializer->base.tok);
  }
  node_t *chain = append_object_zero_fill(var, NULL);
  if (list->entry_count == 0) return chain;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (i > 0 && !entry->has_member) {
      ps_diag_ctx(entry->tok ? entry->tok : initializer->base.tok,
                   "init", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
    }
    int member_index = aggregate_member_index(definition, entry, 0);
    if (member_index < 0 || member_index >= definition->member_count) {
      ps_diag_ctx(entry->tok ? entry->tok : initializer->base.tok,
                   "init", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
    }
    tag_member_info_t *member = &definition->members[member_index];
    node_t *lhs = ps_node_new_tag_member_lvar_ref_for(
        var, member->offset, member);
    chain = append_init(
        chain, ps_node_new_assign(lhs, entry->value));
  }
  return chain;
}

static int aggregate_types_compatible(const psx_type_t *target,
                                      const psx_type_t *value) {
  if (!target || !value || !ps_type_is_tag_aggregate(target) ||
      !ps_type_is_tag_aggregate(value)) return 0;
  if (target->kind != value->kind ||
      ps_type_sizeof(target) != ps_type_sizeof(value)) return 0;
  return ps_type_tag_identity_matches(target, value);
}

static node_t *new_decl_initializer_assign(node_t *target, node_t *value,
                                           token_t *tok) {
  node_t *assign = ps_node_new_assign(target, value);
  assign->is_decl_initializer = 1;
  assign->tok = tok;
  return assign;
}

static node_t *lower_aggregate_expr_initializer(
    node_decl_init_t *initializer, int allow_union_scalar) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *target_type = var ? ps_lvar_get_decl_type(var) : NULL;
  const psx_type_t *value_type = ps_node_get_type(initializer->base.rhs);
  token_t *tok = initializer->base.tok;
  if (!var || !target_type) return NULL;

  if (aggregate_types_compatible(target_type, value_type)) {
    return new_decl_initializer_assign(
        initializer->base.lhs, initializer->base.rhs, tok);
  }
  if (!allow_union_scalar) {
    ps_diag_ctx(tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
  }

  psx_aggregate_definition_t *definition =
      target_type->aggregate_definition;
  if (!definition) {
    ps_diag_ctx(tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  for (int i = 0; i < definition->member_count; i++) {
    tag_member_info_t *member = &definition->members[i];
    if (member->len <= 0) continue;
    node_t *target = ps_node_new_tag_member_lvar_ref_for(
        var, member->offset, member);
    return new_decl_initializer_assign(
        target, initializer->base.rhs, tok);
  }
  ps_diag_ctx(tok, "decl", "%s",
               diag_message_for(
                   DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  return NULL;
}

static int initializer_entry_is_plain_scalar(
    const psx_initializer_entry_t *entry) {
  return entry && entry->value && entry->value->kind != ND_INIT_LIST &&
         entry->designator_count == 0 && !entry->has_index &&
         !entry->has_member;
}

static node_t *lower_scalar_list_initializer(
    node_decl_init_t *initializer) {
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (list->entry_count != 1 ||
      !initializer_entry_is_plain_scalar(&list->entries[0])) {
    ps_diag_ctx(initializer->base.tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
  }
  return new_decl_initializer_assign(
      initializer->base.lhs, list->entries[0].value,
      initializer->base.tok);
}

static node_t *lower_complex_list_initializer(
    node_decl_init_t *initializer, lvar_t *var) {
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (list->entry_count < 1 || list->entry_count > 2) {
    ps_diag_ctx(initializer->base.tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
  }
  for (int i = 0; i < list->entry_count; i++) {
    if (!initializer_entry_is_plain_scalar(&list->entries[i])) {
      ps_diag_ctx(initializer->base.tok, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
    }
  }

  int complex_size = ps_lvar_decl_sizeof(
      var, ps_lvar_storage_size(var, 0));
  int half = complex_size > 0 ? complex_size / 2 : 8;
  node_t *real_lhs = ps_node_new_lvar_fp_slot_for(
      var, ps_lvar_offset(var), half);
  node_t *imag_lhs = ps_node_new_lvar_fp_slot_for(
      var, ps_lvar_offset(var) + half, half);
  node_t *real_assign = new_decl_initializer_assign(
      real_lhs, list->entries[0].value, initializer->base.tok);
  node_t *imag_assign = new_decl_initializer_assign(
      imag_lhs,
      list->entry_count > 1
          ? list->entries[1].value
          : ps_node_new_num(0),
      initializer->base.tok);
  return ps_node_new_binary(ND_COMMA, real_assign, imag_assign);
}

static node_t *lower_typed_list_initializer(
    node_decl_init_t *initializer) {
  if (!initializer || initializer->base.rhs->kind != ND_INIT_LIST)
    return NULL;
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  if (!var || !type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY)
    return lower_array_list_initializer(initializer);
  if (type->kind == PSX_TYPE_STRUCT)
    return lower_struct_list_initializer(initializer);
  if (type->kind == PSX_TYPE_UNION)
    return lower_union_list_initializer(initializer);
  if (type->kind == PSX_TYPE_COMPLEX)
    return lower_complex_list_initializer(initializer, var);
  return lower_scalar_list_initializer(initializer);
}

static node_t *lower_typed_expr_initializer(
    node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  if (!var || !type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    return lower_array_expr_initializer(
        initializer->base.lhs, initializer->base.rhs,
        initializer->base.tok);
  }
  if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION) {
    return lower_aggregate_expr_initializer(
        initializer, type->kind == PSX_TYPE_UNION);
  }
  return new_decl_initializer_assign(
      initializer->base.lhs, initializer->base.rhs,
      initializer->base.tok);
}

void lower_decl_initializer(node_t *node) {
  if (!node || node->kind != ND_DECL_INIT || !node->lhs || !node->rhs) return;

  psx_decl_init_kind_t init_kind = ((node_decl_init_t *)node)->init_kind;
  token_t *tok = node->tok;
  if (init_kind == PSX_DECL_INIT_EXPR) {
    node_t *lowered = lower_typed_expr_initializer(
        (node_decl_init_t *)node);
    if (!lowered) return;
    *node = *lowered;
    node->tok = tok;
    return;
  }

  if (init_kind == PSX_DECL_INIT_LIST) {
    node_t *lowered = lower_typed_list_initializer(
        (node_decl_init_t *)node);
    if (!lowered) return;
    *node = *lowered;
    node->tok = tok;
    return;
  }

}
