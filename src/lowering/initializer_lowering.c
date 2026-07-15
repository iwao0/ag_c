#include "initializer_lowering.h"
#include "runtime_context.h"

#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "../parser/diag.h"
#include "../parser/tag_public.h"
#include "../diag/diag.h"
#include "../semantic/constant_expression.h"
#include "../semantic/initializer_resolution.h"
#include "../type_layout.h"
#include "../tokenizer/literals.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  arena_context_t *arena_context;
  ag_diagnostic_context_t *diagnostic_context;
  const ag_compilation_options_t *options;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_target_info_t *target;
} initializer_lowering_context_t;

static psx_type_id_t type_id(
    const initializer_lowering_context_t *context,
    const psx_type_t *type) {
  if (!context) return PSX_TYPE_ID_INVALID;
  return psx_semantic_type_table_find(
      context->semantic_types, type).type_id;
}

static int type_size(
    const initializer_lowering_context_t *context,
    const psx_type_t *type) {
  if (!context) return 0;
  return ps_type_sizeof_id_with_records(
      context->semantic_types, context->record_layouts,
      type_id(context, type), context->target);
}

static int type_size_id(
    const initializer_lowering_context_t *context,
    psx_type_id_t type_id) {
  if (!context) return 0;
  return ps_type_sizeof_id_with_records(
      context->semantic_types, context->record_layouts,
      type_id, context->target);
}

static int lvar_decl_size(
    const initializer_lowering_context_t *context, const lvar_t *var) {
  if (!context || !var) return 0;
  psx_type_id_t declaration_type_id = ps_lvar_decl_type_id(var);
  return declaration_type_id != PSX_TYPE_ID_INVALID
             ? type_size_id(context, declaration_type_id)
             : type_size(context, ps_lvar_get_decl_type(var));
}

static int lvar_storage_size(
    const initializer_lowering_context_t *context, const lvar_t *var,
    int fallback_size) {
  int declaration_size = lvar_decl_size(context, var);
  int frame_size = ps_lvar_frame_storage_size(var);
  int size = frame_size > declaration_size ? frame_size : declaration_size;
  return size > 0 ? size : fallback_size;
}

static const psx_record_member_layout_t *record_member_layout(
    const initializer_lowering_context_t *context,
    const psx_type_t *aggregate_type, int member_index) {
  if (!context || !aggregate_type ||
      aggregate_type->record_id == PSX_RECORD_ID_INVALID)
    return NULL;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      context->record_layouts, aggregate_type->record_id, context->target);
  return psx_record_layout_member(layout, member_index);
}

static int record_member_offset(
    const initializer_lowering_context_t *context,
    const psx_type_t *aggregate_type, int member_index) {
  const psx_record_member_layout_t *layout = record_member_layout(
      context, aggregate_type, member_index);
  return layout ? layout->offset : -1;
}

static psx_initializer_member_ref_t initializer_member_ref(
    const initializer_lowering_context_t *context,
    const psx_type_t *aggregate_type, int member_index,
    const tag_member_info_t *declaration) {
  psx_initializer_member_ref_t ref = {
      .declaration = declaration,
      .record_id = ps_type_record_id(aggregate_type),
      .member_index = member_index,
  };
  const psx_record_member_layout_t *layout = record_member_layout(
      context, aggregate_type, member_index);
  if (layout) ref.layout = *layout;
  return ref;
}

static node_t *new_initializer_member_lvar_ref(
    const initializer_lowering_context_t *context, lvar_t *owner,
    int relative_offset, const psx_initializer_member_ref_t *member_ref) {
  if (!member_ref || !member_ref->declaration) return NULL;
  return ps_node_new_tag_member_lvar_ref_with_layout_for_in(
      context->arena_context, owner, relative_offset,
      member_ref->declaration, member_ref->layout.bit_width,
      member_ref->layout.bit_offset);
}

static const psx_record_decl_t *record_decl(
    const initializer_lowering_context_t *context,
    const psx_type_t *type) {
  return context && type
             ? psx_record_decl_table_lookup(
                   context->record_decls, ps_type_record_id(type))
             : NULL;
}

static ag_diagnostic_context_t *diagnostics(
    const initializer_lowering_context_t *context) {
  return context->diagnostic_context;
}

static node_t *append_init(
    const initializer_lowering_context_t *context,
    node_t *chain, node_t *item) {
  return chain
             ? ps_node_new_binary_for_target_in(
                   context->arena_context, context->target,
                   ND_COMMA, chain, item)
             : item;
}

static int initializer_value_is_zero(const node_t *value) {
  if (!value || value->kind != ND_NUM) return 0;
  const node_num_t *number = (const node_num_t *)value;
  return ps_node_value_fp_kind((node_t *)value) != TK_FLOAT_KIND_NONE
             ? number->fval == 0.0
             : number->val == 0;
}

static int initializer_supports_recursive_aggregate(
    const initializer_lowering_context_t *context,
    const psx_type_t *type) {
  type = ps_type_array_leaf_type(type);
  if (!type || !ps_type_is_tag_aggregate(type)) return 0;
  const psx_record_decl_t *record = record_decl(context, type);
  if (!record) return 0;
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *member = &record->members[i];
    if (member->len <= 0) {
      const psx_type_t *member_type = ps_tag_member_decl_type(member);
      if (!member_type || !ps_type_is_tag_aggregate(member_type) ||
          !initializer_supports_recursive_aggregate(context, member_type))
        return 0;
      continue;
    }
    const psx_type_t *member_type = ps_tag_member_decl_type(member);
    const psx_type_t *leaf = ps_type_array_leaf_type(member_type);
    if (leaf && ps_type_is_tag_aggregate(leaf) &&
        !initializer_supports_recursive_aggregate(context, member_type))
      return 0;
  }
  return 1;
}

static node_t *new_array_elem_assign(
    const initializer_lowering_context_t *context,
    lvar_t *var, int idx, node_t *value) {
  const psx_type_t *array_type = ps_lvar_get_decl_type(var);
  const psx_type_t *element = ps_type_array_leaf_type(array_type);
  int element_size = type_size(context, element);
  node_t *target = ps_node_new_lvar_type_at_for_in(
      context->arena_context, var,
      ps_lvar_offset(var) + idx * (element_size > 0 ? element_size : 0),
      element);
  return ps_node_new_assign_in(
      context->arena_context, target, value);
}

static int array_leaf_size(
    const initializer_lowering_context_t *context, const lvar_t *var) {
  return type_size(
      context, ps_type_array_leaf_type(ps_lvar_get_decl_type(var)));
}

typedef struct {
  const initializer_lowering_context_t *lowering;
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
      ctx->lowering, ctx->chain,
      new_array_elem_assign(
          ctx->lowering, ctx->var, ctx->idx,
          ps_node_new_num_in(ctx->lowering->arena_context, unit)));
  if (ctx->assigned) ctx->assigned[ctx->idx] = 1;
  ctx->idx++;
}

static node_t *lower_array_string_initializer_at(
    const initializer_lowering_context_t *context,
    lvar_t *var, node_string_t *string, int base_index, int capacity,
    int array_len, unsigned char *assigned, node_t *chain, token_t *tok) {
  if (!string->literal_contents) {
    ps_diag_ctx_in(diagnostics(context), tok, "init", "%s",
                 diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }

  int elem_size = array_leaf_size(context, var);
  if (elem_size <= 0) elem_size = 1;
  int limit = base_index + capacity;
  if (limit > array_len) limit = array_len;
  array_string_lowering_ctx_t ctx = {
      context, var, chain, base_index, limit, assigned};
  tk_emit_string_code_units(
      string->literal_contents, string->literal_length, elem_size, capacity,
                            append_array_string_unit, &ctx);
  while (ctx.idx < limit) {
    ctx.chain = append_init(
        context, ctx.chain,
        new_array_elem_assign(
            context, var, ctx.idx,
            ps_node_new_num_in(context->arena_context, 0)));
    if (assigned) assigned[ctx.idx] = 1;
    ctx.idx++;
  }
  return ctx.chain
             ? ctx.chain
             : ps_node_new_num_in(context->arena_context, 0);
}

static node_t *lower_array_string_initializer(
    const initializer_lowering_context_t *context,
    lvar_t *var, node_string_t *string, int array_len, token_t *tok) {
  return lower_array_string_initializer_at(
      context, var, string, 0, array_len, array_len, NULL, NULL, tok);
}

static node_t *lower_array_expr_initializer(
    const initializer_lowering_context_t *context,
    node_t *target, node_t *value, token_t *tok) {
  lvar_t *var = ps_node_lvar_symbol(target);
  if (!var) return NULL;
  int elem_size = array_leaf_size(context, var);
  int array_len = ps_lvar_array_flat_element_count(var);
  if (array_len <= 0 && elem_size > 0) {
    array_len = lvar_storage_size(context, var, 0) / elem_size;
  }

  if (value->kind == ND_STRING) {
    int char_width = (int)((node_string_t *)value)->char_width;
    if (char_width <= 0) char_width = 1;
    if (elem_size == char_width) {
      return lower_array_string_initializer(
          context, var, (node_string_t *)value, array_len, tok);
    }
  }

  if (array_len > 0 && value->kind == ND_COMMA && value->rhs &&
      value->rhs->kind == ND_ADDR && value->rhs->lhs &&
      value->rhs->lhs->kind == ND_LVAR) {
    node_lvar_t *source = (node_lvar_t *)value->rhs->lhs;
    node_t *chain = value->lhs;
    for (int i = 0; i < array_len; i++) {
      const psx_type_t *element =
          ps_type_array_leaf_type(ps_lvar_get_decl_type(var));
      node_t *dst = ps_node_new_lvar_type_at_for_in(
          context->arena_context, var,
          ps_lvar_offset(var) + i * elem_size, element);
      node_t *src = ps_node_new_lvar_type_at_for_in(
          context->arena_context, source->var,
          source->offset + i * elem_size, element);
      chain = append_init(
          context, chain,
          ps_node_new_assign_in(context->arena_context, dst, src));
    }
    return chain;
  }

  if (array_len > 0) {
    node_t *chain = new_array_elem_assign(context, var, 0, value);
    for (int i = 1; i < array_len; i++) {
      chain = append_init(
          context, chain,
          new_array_elem_assign(
              context, var, i,
              ps_node_new_num_in(context->arena_context, 0)));
    }
    return chain;
  }
  return ps_node_new_num_in(context->arena_context, 0);
}

static int resolve_initializer_entry_index(
    const initializer_lowering_context_t *context,
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
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init",
                   diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                   diag_text_for_in(diagnostics(context), DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
    }
    if (resolved < 0) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init",
                   diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                   diag_text_for_in(diagnostics(context), DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
    }
    int stride = ps_lvar_array_designator_stride_elements(var, depth + d);
    if (stride <= 0) stride = 1;
    index += (int)resolved * stride;
  }
  return index;
}

static int entry_initializes_character_subarray(
    const initializer_lowering_context_t *context,
    lvar_t *var, const node_t *value, int stride) {
  if (!var || !value || value->kind != ND_STRING || stride <= 1) return 0;
  int elem_size = array_leaf_size(context, var);
  int char_width = (int)((const node_string_t *)value)->char_width;
  if (char_width <= 0) char_width = 1;
  return elem_size == char_width;
}

static void lower_array_initializer_entries(
    const initializer_lowering_context_t *context,
    lvar_t *var, node_init_list_t *list, int depth, int base_index,
    int array_len, unsigned char *assigned, node_t **chain,
    token_t *fallback_tok) {
  int stride = ps_lvar_array_designator_stride_elements(var, depth);
  if (stride <= 0) stride = 1;
  int next_index = base_index;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    int index = resolve_initializer_entry_index(
        context, entry, fallback_tok, var,
        depth, next_index, base_index);
    if (index < 0 || index >= array_len) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    if (entry->value && entry->value->kind == ND_INIT_LIST) {
      lower_array_initializer_entries(
          context, var, (node_init_list_t *)entry->value, depth + 1, index,
          array_len, assigned, chain, fallback_tok);
      next_index = index + stride;
    } else if (entry_initializes_character_subarray(
                   context, var, entry->value, stride)) {
      if (index + stride > array_len) {
        ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok,
                     "init", "%s",
                     diag_message_for_in(diagnostics(context),
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      *chain = lower_array_string_initializer_at(
          context, var, (node_string_t *)entry->value, index, stride,
          array_len, assigned, *chain,
          entry->tok ? entry->tok : fallback_tok);
      next_index = index + stride;
    } else {
      *chain = append_init(
          context, *chain,
          new_array_elem_assign(context, var, index, entry->value));
      assigned[index] = 1;
      next_index = index + 1;
    }
  }
}

static node_t *lower_typed_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok);
static node_t *append_object_zero_fill(
    const initializer_lowering_context_t *context,
    lvar_t *var, node_t *chain);
static node_t *append_typed_object_zero_fill(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    node_t *chain);
static node_t *try_lower_typed_array_copy(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_t *value, node_t *chain);

typedef struct {
  const initializer_lowering_context_t *lowering;
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
  node_t *target = ps_node_new_lvar_type_at_for_in(
      ctx->lowering->arena_context, ctx->var,
      ps_lvar_offset(ctx->var) + ctx->relative_offset +
          ctx->index * ctx->element_size,
      ctx->element_type);
  ctx->chain = append_init(
      ctx->lowering, ctx->chain,
      ps_node_new_assign_in(
          ctx->lowering->arena_context,
          target,
          ps_node_new_num_in(ctx->lowering->arena_context, unit)));
  ctx->index++;
}

static node_t *lower_typed_character_array_string(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *array_type,
    psx_type_id_t array_type_id, int relative_offset,
    node_string_t *string, node_t *chain, token_t *tok) {
  const psx_type_t *element = ps_type_array_leaf_type(array_type);
  psx_type_id_t element_type_id = psx_semantic_type_table_array_leaf(
      context->semantic_types, array_type_id).type_id;
  int element_size = type_size_id(context, element_type_id);
  int capacity = element_size > 0
                     ? type_size_id(context, array_type_id) / element_size
                     : 0;
  int char_width = (int)string->char_width;
  if (char_width <= 0) char_width = 1;
  if (!element || capacity <= 0 || element_size != char_width) return NULL;

  if (!string->literal_contents) {
    ps_diag_ctx_in(diagnostics(context), tok, "init", "%s",
                 diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }
  typed_string_lowering_ctx_t ctx = {
      context, var, element, chain, relative_offset,
      element_size, 0, capacity};
  tk_emit_string_code_units(
      string->literal_contents, string->literal_length,
      element_size, capacity,
                            append_typed_string_unit, &ctx);
  while (ctx.index < capacity)
    append_typed_string_unit(0, &ctx);
  return ctx.chain;
}

static int type_has_aggregate_leaf(const psx_type_t *type) {
  return ps_type_is_tag_aggregate(ps_type_array_leaf_type(type));
}

static node_t *lower_array_list_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer) {
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
          context, var, type, ps_lvar_decl_type_id(var), 0,
          (node_string_t *)entry->value, NULL,
          entry->tok ? entry->tok : initializer->base.tok);
      if (lowered) return lowered;
    }
  }
  if (type_has_aggregate_leaf(type)) {
    node_t *chain = append_typed_object_zero_fill(
        context, var, type, ps_lvar_decl_type_id(var), NULL);
    return lower_typed_initializer_list(
        context, var, type, ps_lvar_decl_type_id(var), 0,
        list, chain, initializer->base.tok);
  }
  int array_len = ps_lvar_array_flat_element_count(var);
  int elem_size = array_leaf_size(context, var);
  if (array_len <= 0 && elem_size > 0) {
    array_len = lvar_storage_size(context, var, 0) / elem_size;
  }

  unsigned char *assigned = calloc(
      (size_t)(array_len > 0 ? array_len : 1), 1);
  if (!assigned) return NULL;
  node_t *chain = NULL;
  lower_array_initializer_entries(
      context, var, list, 0, 0, array_len, assigned, &chain,
      initializer->base.tok);
  for (int i = 0; i < array_len; i++) {
    if (!assigned[i]) {
      chain = append_init(
          context, chain,
          new_array_elem_assign(
              context, var, i,
              ps_node_new_num_in(context->arena_context, 0)));
    }
  }
  free(assigned);
  return chain
             ? chain
             : ps_node_new_num_in(context->arena_context, 0);
}

static node_t *append_object_zero_fill(
    const initializer_lowering_context_t *context,
    lvar_t *var, node_t *chain) {
  int size = lvar_decl_size(context, var);
  if (size <= 0) size = lvar_storage_size(context, var, 0);
  int offset = 0;
  const int widths[] = {8, 4, 2, 1};
  for (int w = 0; w < 4; w++) {
    int width = widths[w];
    while (offset + width <= size) {
      node_t *slot = ps_node_new_lvar_storage_slot_for_in(
          context->arena_context, var,
          ps_lvar_offset(var) + offset, width);
      chain = append_init(
          context, chain,
          ps_node_new_assign_in(
              context->arena_context,
              slot, ps_node_new_num_in(context->arena_context, 0)));
      offset += width;
    }
  }
  return chain;
}

static int aggregate_member_index(
    const initializer_lowering_context_t *context,
    const psx_type_t *aggregate_type, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record,
    const psx_initializer_entry_t *entry, int positional_index) {
  if (!entry->has_member) {
    int covered_end = -1;
    for (int i = 0; i < record->member_count; i++) {
      const tag_member_info_t *member = &record->members[i];
      psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
          context->semantic_types, aggregate_type_id, i).type_id;
      int member_size = type_size_id(context, member_type_id);
      int member_offset = record_member_offset(
          context, aggregate_type, i);
      if (member->len <= 0 && member_size > 0) {
        if (i >= positional_index) return i;
        int end = member_offset + member_size;
        if (end > covered_end) covered_end = end;
        continue;
      }
      if (member_offset < covered_end) continue;
      if (i >= positional_index) return i;
    }
    return -1;
  }
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *member = &record->members[i];
    if (member->len == entry->member_len && member->name &&
        memcmp(member->name, entry->member_name,
               (size_t)entry->member_len) == 0) return i;
  }
  return -1;
}

static int aggregate_ordinal_after_member(
    const initializer_lowering_context_t *context,
    const psx_type_t *aggregate_type, psx_type_id_t aggregate_type_id,
    const psx_record_decl_t *record, int member_index) {
  if (!record || member_index < 0 ||
      member_index >= record->member_count) return member_index + 1;
  int selected_offset = record_member_offset(
      context, aggregate_type, member_index);
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *container = &record->members[i];
    if (container->len > 0) continue;
    psx_type_id_t container_type_id =
        psx_semantic_type_table_record_member(
            context->semantic_types, aggregate_type_id, i).type_id;
    int size = type_size_id(context, container_type_id);
    if (size <= 0) continue;
    int start = record_member_offset(context, aggregate_type, i);
    int end = start + size;
    if (selected_offset < start || selected_offset >= end) continue;
    int next = i + 1;
    while (next < record->member_count &&
           record_member_offset(context, aggregate_type, next) < end)
      next++;
    return next;
  }
  return member_index + 1;
}

static long long resolve_typed_designator_index(
    const initializer_lowering_context_t *context,
    const psx_initializer_entry_t *entry, int index_pos,
    token_t *fallback_tok) {
  int ok = 1;
  long long index = psx_eval_const_int(entry->index_exprs[index_pos], &ok);
  if (!ok) {
    ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init",
                 diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                 diag_text_for_in(diagnostics(context), DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  if (index < 0) {
    ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init",
                 diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                 diag_text_for_in(diagnostics(context), DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  return index;
}

typedef psx_initializer_target_t typed_designator_target_t;

static int descend_array_designators(
    const initializer_lowering_context_t *context,
    const psx_initializer_entry_t *entry, int first_index_pos,
    const psx_type_t **type_inout, psx_type_id_t *type_id_inout,
    int *relative_offset_inout,
    token_t *fallback_tok) {
  int first_index = -1;
  for (int d = first_index_pos; d < entry->index_expr_count; d++) {
    const psx_type_t *array = *type_inout;
    if (!array || array->kind != PSX_TYPE_ARRAY || !array->base) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    long long index = resolve_typed_designator_index(
        context, entry, d, fallback_tok);
    if (index >= array->array_len) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    if (first_index < 0) first_index = (int)index;
    psx_type_id_t child_type_id = psx_semantic_type_table_base(
        context->semantic_types, *type_id_inout).type_id;
    int child_size = type_size_id(context, child_type_id);
    *relative_offset_inout += (int)index * child_size;
    *type_inout = array->base;
    *type_id_inout = child_type_id;
  }
  return first_index;
}

static node_t *lower_typed_initializer_value(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset, node_t *value,
    const psx_initializer_member_ref_t *member_ref, node_t *chain,
    token_t *fallback_tok) {
  if (value && value->kind == ND_INIT_LIST) {
    return lower_typed_initializer_list(
        context, var, type, type_id, relative_offset,
        (node_init_list_t *)value, chain, fallback_tok);
  }
  if (value && value->kind == ND_STRING && type &&
      type->kind == PSX_TYPE_ARRAY) {
    node_t *lowered = lower_typed_character_array_string(
        context, var, type, type_id, relative_offset,
        (node_string_t *)value, chain, fallback_tok);
    if (lowered) return lowered;
  }
  if (value && type && type->kind == PSX_TYPE_ARRAY) {
    node_t *copied = try_lower_typed_array_copy(
        context, var, type, type_id, relative_offset, value, chain);
    if (copied) return copied;
  }
  if (initializer_value_is_zero(value) && type &&
      (type->kind == PSX_TYPE_ARRAY || ps_type_is_tag_aggregate(type))) {
    return chain;
  }
  node_t *target = member_ref && member_ref->declaration
                       ? new_initializer_member_lvar_ref(
                             context, var, relative_offset, member_ref)
                       : ps_node_new_lvar_type_at_for_in(
                             context->arena_context, var,
                             ps_lvar_offset(var) + relative_offset, type);
  return append_init(
      context, chain,
      ps_node_new_assign_in(context->arena_context, target, value));
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
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    node_t *chain) {
  (void)type;
  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->target, type_id, 0, &leaves)) {
    free(leaves.items);
    return append_object_zero_fill(context, var, chain);
  }
  for (int i = 0; i < leaves.count; i++) {
    const typed_scalar_leaf_t *leaf = &leaves.items[i];
    node_t *target = leaf->member_ref.declaration
                         ? new_initializer_member_lvar_ref(
                               context, var, leaf->relative_offset,
                               &leaf->member_ref)
                         : ps_node_new_lvar_type_at_for_in(
                               context->arena_context, var,
                               ps_lvar_offset(var) + leaf->relative_offset,
                               leaf->type);
    chain = append_init(
        context, chain,
        ps_node_new_assign_in(
            context->arena_context,
            target, ps_node_new_num_in(context->arena_context, 0)));
  }
  free(leaves.items);
  return chain;
}

static node_t *lower_flat_typed_object_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, psx_type_id_t type_id, int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->target, type_id,
          relative_offset, &leaves)) {
    free(leaves.items);
    ps_diag_ctx_in(diagnostics(context), fallback_tok, "init", "%s",
                 diag_message_for_in(diagnostics(context),
                     DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
  }
  int leaf_index = 0;
  for (int i = 0; i < list->entry_count; i++) {
    if (leaf_index >= leaves.count) {
      free(leaves.items);
      ps_diag_ctx_in(diagnostics(context), list->entries[i].tok ? list->entries[i].tok : fallback_tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const typed_scalar_leaf_t *leaf = &leaves.items[leaf_index];
    if (list->entries[i].value->kind == ND_STRING &&
        leaf->string_array_type) {
      node_t *lowered = lower_typed_character_array_string(
          context, var, leaf->string_array_type,
          leaf->string_array_type_id, leaf->string_array_offset,
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
        context, var, leaf->type, leaf->type_id, leaf->relative_offset,
        list->entries[i].value,
        leaf->member_ref.declaration ? &leaf->member_ref : NULL,
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
    const initializer_lowering_context_t *context,
    const psx_type_t *type, psx_type_id_t type_id, int relative_offset,
    const typed_scalar_leaf_list_t *leaves, int leaf_cursor,
    typed_designator_target_t *out) {
  if (!type || !leaves || leaf_cursor < 0 || leaf_cursor >= leaves->count ||
      !out) return 0;
  int leaf_offset = leaves->items[leaf_cursor].relative_offset;
  if (type->kind == PSX_TYPE_ARRAY && type->base) {
    psx_type_id_t child_type_id = psx_semantic_type_table_base(
        context->semantic_types, type_id).type_id;
    int child_size = type_size_id(context, child_type_id);
    if (child_size <= 0 || leaf_offset < relative_offset) return 0;
    int child_index = (leaf_offset - relative_offset) / child_size;
    int child_offset = relative_offset + child_index * child_size;
    if (child_index < 0 || child_index >= type->array_len ||
        child_offset != leaf_offset) return 0;
    *out = (typed_designator_target_t){
        .type = type->base,
        .type_id = child_type_id,
        .relative_offset = child_offset,
        .first_array_index = child_index,
        .first_member_index = -1,
    };
    return type->base->kind == PSX_TYPE_ARRAY ||
           ps_type_is_tag_aggregate(type->base);
  }
  if (type->kind == PSX_TYPE_STRUCT) {
    const psx_record_decl_t *record = record_decl(context, type);
    for (int i = 0; record && i < record->member_count; i++) {
      const tag_member_info_t *member = &record->members[i];
      const psx_type_t *member_type = ps_tag_member_decl_type(member);
      int member_offset = relative_offset +
                          record_member_offset(context, type, i);
      if (member_offset != leaf_offset || !member_type) continue;
      if (member_type->kind != PSX_TYPE_ARRAY &&
          !ps_type_is_tag_aggregate(member_type)) return 0;
      *out = (typed_designator_target_t){
          .type = member_type,
          .type_id = psx_semantic_type_table_record_member(
              context->semantic_types, type_id, i).type_id,
          .relative_offset = member_offset,
          .member_ref = initializer_member_ref(
              context, type, i, member),
          .first_array_index = -1,
          .first_member_index = i,
      };
      return 1;
    }
  }
  return 0;
}

static node_t *lower_mixed_typed_object_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->target, type_id,
          relative_offset, &leaves)) {
    free(leaves.items);
    return chain;
  }
  int leaf_cursor = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->designator_count > 0) {
      typed_designator_target_t target =
          psx_resolve_initializer_designator_path_with_records(
              diagnostics(context), context->semantic_types,
              context->record_decls, context->record_layouts,
              context->target, entry, type_id, relative_offset,
              fallback_tok);
      chain = lower_typed_initializer_value(
          context, var, target.type, target.type_id,
          target.relative_offset, entry->value,
          target.member_ref.declaration ? &target.member_ref : NULL, chain,
          entry->tok ? entry->tok : fallback_tok);
      leaf_cursor = psx_initializer_leaf_cursor_after_target_with_records(
          context->semantic_types, context->record_layouts,
          context->target, &leaves, &target);
      continue;
    }
    if (leaf_cursor >= leaves.count) {
      free(leaves.items);
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const typed_scalar_leaf_t *leaf = &leaves.items[leaf_cursor];
    if (initializer_value_requires_immediate_subobject(entry->value)) {
      typed_designator_target_t target = {0};
      if (immediate_subobject_at_leaf_cursor(
              context, type, type_id, relative_offset,
              &leaves, leaf_cursor,
              &target)) {
        chain = lower_typed_initializer_value(
            context, var, target.type, target.type_id,
            target.relative_offset, entry->value,
            target.member_ref.declaration ? &target.member_ref : NULL, chain,
            entry->tok ? entry->tok : fallback_tok);
        leaf_cursor = psx_initializer_leaf_cursor_after_target_with_records(
            context->semantic_types, context->record_layouts,
            context->target, &leaves, &target);
        continue;
      }
    }
    if (entry->value->kind == ND_STRING && leaf->string_array_type) {
      node_t *lowered = lower_typed_character_array_string(
          context, var, leaf->string_array_type,
          leaf->string_array_type_id, leaf->string_array_offset,
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
        context, var, leaf->type, leaf->type_id,
        leaf->relative_offset, entry->value,
        leaf->member_ref.declaration ? &leaf->member_ref : NULL, chain,
        entry->tok ? entry->tok : fallback_tok);
    leaf_cursor++;
  }
  free(leaves.items);
  return chain;
}

static node_t *try_lower_typed_array_copy(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_t *value, node_t *chain) {
  if (!var || !type || type->kind != PSX_TYPE_ARRAY || !value ||
      value->kind != ND_ADDR || !value->lhs ||
      value->lhs->kind != ND_LVAR) return NULL;
  node_lvar_t *source = (node_lvar_t *)value->lhs;
  const psx_type_t *source_type = ps_node_get_type(value->lhs);
  if (!source->var || !source_type ||
      type_size_id(context, ps_lvar_decl_type_id(source->var)) <
          type_size_id(context, type_id)) return NULL;

  typed_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->target, type_id,
          relative_offset, &leaves)) {
    free(leaves.items);
    return NULL;
  }
  for (int i = 0; i < leaves.count; i++) {
    const typed_scalar_leaf_t *leaf = &leaves.items[i];
    int source_offset = source->offset +
                        (leaf->relative_offset - relative_offset);
    node_t *src = ps_node_new_lvar_type_at_for_in(
        context->arena_context, source->var, source_offset, leaf->type);
    node_t *dst = leaf->member_ref.declaration
                      ? new_initializer_member_lvar_ref(
                            context, var, leaf->relative_offset,
                            &leaf->member_ref)
                      : ps_node_new_lvar_type_at_for_in(
                            context->arena_context, var,
                            ps_lvar_offset(var) + leaf->relative_offset,
                            leaf->type);
    chain = append_init(
        context, chain,
        ps_node_new_assign_in(context->arena_context, dst, src));
  }
  free(leaves.items);
  return chain;
}

static node_t *lower_flat_typed_array_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  const psx_type_t *leaf = ps_type_array_leaf_type(type);
  int leaf_size = type_size_id(
      context, psx_semantic_type_table_array_leaf(
                   context->semantic_types, type_id).type_id);
  int flat_count = ps_type_array_flat_element_count(type);
  if (!leaf || flat_count <= 0 || list->entry_count > flat_count) {
    ps_diag_ctx_in(diagnostics(context), fallback_tok, "init", "%s",
                 diag_message_for_in(diagnostics(context),
                     DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  }
  for (int i = 0; i < list->entry_count; i++) {
    node_t *target = ps_node_new_lvar_type_at_for_in(
        context->arena_context, var,
        ps_lvar_offset(var) + relative_offset + i * leaf_size, leaf);
    chain = append_init(
        context, chain,
        ps_node_new_assign_in(
            context->arena_context, target, list->entries[i].value));
  }
  return chain;
}

static node_t *lower_typed_array_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  if (list->entry_count == 1 &&
      initializer_value_is_zero(list->entries[0].value) &&
      list->entries[0].designator_count == 0) return chain;
  if (type_has_aggregate_leaf(type) &&
      initializer_list_has_no_nested_values(list)) {
    return lower_mixed_typed_object_initializer_list(
        context, var, type, type_id, relative_offset,
        list, chain, fallback_tok);
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
          context, var, type_id, relative_offset,
          list, chain, fallback_tok);
    }
    return lower_flat_typed_array_initializer_list(
        context, var, type, type_id, relative_offset,
        list, chain, fallback_tok);
  }
  int next_index = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->has_member && entry->designator_count == 0) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    const psx_type_t *target_type = type->base;
    psx_type_id_t target_type_id = psx_semantic_type_table_base(
        context->semantic_types, type_id).type_id;
    int target_offset = relative_offset;
    int selected_index = next_index;
    if (entry->designator_count > 0) {
      if (entry->designators[0].kind != PSX_INIT_DESIGNATOR_INDEX) {
        ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                     diag_message_for_in(diagnostics(context),
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      typed_designator_target_t target =
          psx_resolve_initializer_designator_path_with_records(
              diagnostics(context), context->semantic_types,
              context->record_decls, context->record_layouts,
              context->target, entry, type_id, relative_offset,
              fallback_tok);
      target_type = target.type;
      target_type_id = target.type_id;
      target_offset = target.relative_offset;
      selected_index = target.first_array_index;
      chain = lower_typed_initializer_value(
          context, var, target_type, target_type_id,
          target_offset, entry->value,
          target.member_ref.declaration ? &target.member_ref : NULL, chain,
          entry->tok ? entry->tok : fallback_tok);
      next_index = selected_index + 1;
      continue;
    } else if (entry->index_expr_count > 0) {
      target_type = type;
      target_type_id = type_id;
      selected_index = descend_array_designators(
          context, entry, 0, &target_type, &target_type_id,
          &target_offset, fallback_tok);
    } else {
      if (selected_index < 0 || selected_index >= type->array_len) {
        ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                     diag_message_for_in(diagnostics(context),
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      target_offset += selected_index *
                       type_size_id(context, target_type_id);
    }
    chain = lower_typed_initializer_value(
        context, var, target_type, target_type_id,
        target_offset, entry->value, NULL,
        chain, entry->tok ? entry->tok : fallback_tok);
    next_index = selected_index + 1;
  }
  return chain;
}

static node_t *lower_typed_aggregate_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  const psx_record_decl_t *record = record_decl(context, type);
  if (!record) return chain;
  if (type->kind == PSX_TYPE_UNION && record->member_count > 0 &&
      initializer_list_has_no_nested_values(list)) {
    int has_designator = 0;
    for (int i = 0; i < list->entry_count; i++) {
      if (list->entries[i].designator_count > 0 ||
          list->entries[i].has_member || list->entries[i].has_index) {
        has_designator = 1;
        break;
      }
    }
    const tag_member_info_t *first = &record->members[0];
    const psx_type_t *first_type = ps_tag_member_decl_type(first);
    if (!has_designator && first_type &&
        first_type->kind == PSX_TYPE_ARRAY) {
      if (!context->options->enable_union_array_member_nonbrace_init) {
        ps_diag_ctx_in(diagnostics(context), fallback_tok, "decl", "%s",
                     diag_message_for_in(diagnostics(context),
                         DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED));
      }
      return lower_typed_array_initializer_list(
          context, var, first_type,
          psx_semantic_type_table_record_member(
              context->semantic_types, type_id, 0).type_id,
          relative_offset + record_member_offset(context, type, 0),
          list, chain, fallback_tok);
    }
  }
  if (list->entry_count == 1 &&
      initializer_value_is_zero(list->entries[0].value) &&
      list->entries[0].designator_count == 0) return chain;
  if (type->kind == PSX_TYPE_STRUCT &&
      initializer_list_has_no_nested_values(list)) {
    return lower_mixed_typed_object_initializer_list(
        context, var, type, type_id, relative_offset,
        list, chain, fallback_tok);
  }
  if (type->kind == PSX_TYPE_STRUCT &&
      initializer_list_is_flat_positional_scalar(list)) {
    return lower_flat_typed_object_initializer_list(
        context, var, type_id, relative_offset,
        list, chain, fallback_tok);
  }
  int ordinal = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (type->kind == PSX_TYPE_UNION && i > 0 &&
        entry->designator_count == 0 && !entry->has_member) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
    }
    if (entry->designator_count > 0) {
      if (entry->designators[0].kind != PSX_INIT_DESIGNATOR_MEMBER) {
        ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                     diag_message_for_in(diagnostics(context),
                         DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      typed_designator_target_t target =
          psx_resolve_initializer_designator_path_with_records(
              diagnostics(context), context->semantic_types,
              context->record_decls, context->record_layouts,
              context->target, entry, type_id, relative_offset,
              fallback_tok);
      chain = lower_typed_initializer_value(
          context, var, target.type, target.type_id,
          target.relative_offset, entry->value,
          target.member_ref.declaration ? &target.member_ref : NULL, chain,
          entry->tok ? entry->tok : fallback_tok);
      if (type->kind == PSX_TYPE_STRUCT)
        ordinal = aggregate_ordinal_after_member(
            context, type, type_id, record, target.first_member_index);
      continue;
    }
    int member_index = aggregate_member_index(
        context, type, type_id, record, entry, ordinal);
    if (member_index < 0 || member_index >= record->member_count) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : fallback_tok, "init", "%s",
                   diag_message_for_in(diagnostics(context),
                       type->kind == PSX_TYPE_UNION
                           ? DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND
                           : DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const tag_member_info_t *member = &record->members[member_index];
    const psx_type_t *target_type = ps_tag_member_decl_type(member);
    psx_type_id_t target_type_id = psx_semantic_type_table_record_member(
        context->semantic_types, type_id, member_index).type_id;
    int target_offset = relative_offset +
                        record_member_offset(context, type, member_index);
    psx_initializer_member_ref_t member_ref = initializer_member_ref(
        context, type, member_index, member);
    if (entry->index_expr_count > 0) {
      descend_array_designators(
          context, entry, 0, &target_type, &target_type_id,
          &target_offset, fallback_tok);
      member_ref = (psx_initializer_member_ref_t){0};
    }
    chain = lower_typed_initializer_value(
        context, var, target_type, target_type_id,
        target_offset, entry->value,
        member_ref.declaration ? &member_ref : NULL,
        chain, entry->tok ? entry->tok : fallback_tok);
    if (type->kind == PSX_TYPE_STRUCT)
      ordinal = aggregate_ordinal_after_member(
          context, type, type_id, record, member_index);
  }
  return chain;
}

static node_t *lower_typed_initializer_list(
    const initializer_lowering_context_t *context,
    lvar_t *var, const psx_type_t *type, psx_type_id_t type_id,
    int relative_offset,
    node_init_list_t *list, node_t *chain, token_t *fallback_tok) {
  if (!type || !list) return chain;
  if (type->kind == PSX_TYPE_ARRAY) {
    return lower_typed_array_initializer_list(
        context, var, type, type_id, relative_offset,
        list, chain, fallback_tok);
  }
  if (ps_type_is_tag_aggregate(type)) {
    return lower_typed_aggregate_initializer_list(
        context, var, type, type_id, relative_offset,
        list, chain, fallback_tok);
  }
  if (list->entry_count == 1) {
    return lower_typed_initializer_value(
        context, var, type, type_id,
        relative_offset, list->entries[0].value, NULL,
        chain, fallback_tok);
  }
  ps_diag_ctx_in(diagnostics(context), fallback_tok, "init", "%s",
               diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
  return chain;
}

static node_t *lower_struct_list_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  const psx_record_decl_t *record = record_decl(context, type);
  if (!var || !record || !initializer->base.rhs ||
      initializer->base.rhs->kind != ND_INIT_LIST) return NULL;
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (initializer_supports_recursive_aggregate(context, type)) {
    node_t *chain = append_typed_object_zero_fill(
        context, var, type, ps_lvar_decl_type_id(var), NULL);
    return lower_typed_initializer_list(
        context, var, type, ps_lvar_decl_type_id(var), 0,
        list, chain, initializer->base.tok);
  }
  node_t *chain = append_object_zero_fill(context, var, NULL);
  int ordinal = 0;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    int member_index = aggregate_member_index(
        context, type, ps_lvar_decl_type_id(var),
        record, entry, ordinal);
    if (member_index < 0 || member_index >= record->member_count) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : initializer->base.tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    const tag_member_info_t *member = &record->members[member_index];
    psx_initializer_member_ref_t member_ref = initializer_member_ref(
        context, type, member_index, member);
    node_t *lhs = new_initializer_member_lvar_ref(
        context, var, member_ref.layout.offset, &member_ref);
    chain = append_init(
        context, chain,
        ps_node_new_assign_in(
            context->arena_context, lhs, entry->value));
    ordinal = aggregate_ordinal_after_member(
        context, type, ps_lvar_decl_type_id(var), record, member_index);
  }
  return chain
             ? chain
             : ps_node_new_num_in(context->arena_context, 0);
}

static node_t *lower_union_list_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  const psx_record_decl_t *record = record_decl(context, type);
  if (!var || !record || !initializer->base.rhs ||
      initializer->base.rhs->kind != ND_INIT_LIST) return NULL;
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (initializer_supports_recursive_aggregate(context, type)) {
    node_t *chain = append_typed_object_zero_fill(
        context, var, type, ps_lvar_decl_type_id(var), NULL);
    return lower_typed_initializer_list(
        context, var, type, ps_lvar_decl_type_id(var), 0,
        list, chain, initializer->base.tok);
  }
  node_t *chain = append_object_zero_fill(context, var, NULL);
  if (list->entry_count == 0) return chain;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (i > 0 && !entry->has_member) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : initializer->base.tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
    }
    int member_index = aggregate_member_index(
        context, type, ps_lvar_decl_type_id(var), record, entry, 0);
    if (member_index < 0 || member_index >= record->member_count) {
      ps_diag_ctx_in(diagnostics(context), entry->tok ? entry->tok : initializer->base.tok,
                   "init", "%s",
                   diag_message_for_in(diagnostics(context), DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
    }
    const tag_member_info_t *member = &record->members[member_index];
    psx_initializer_member_ref_t member_ref = initializer_member_ref(
        context, type, member_index, member);
    node_t *lhs = new_initializer_member_lvar_ref(
        context, var, member_ref.layout.offset, &member_ref);
    chain = append_init(
        context, chain,
        ps_node_new_assign_in(
            context->arena_context, lhs, entry->value));
  }
  return chain;
}

static int aggregate_types_compatible(
                                      const initializer_lowering_context_t *context,
                                      const psx_type_t *target,
                                      const psx_type_t *value) {
  if (!target || !value || !ps_type_is_tag_aggregate(target) ||
      !ps_type_is_tag_aggregate(value)) return 0;
  if (target->kind != value->kind ||
      type_size(context, target) != type_size(context, value)) return 0;
  return ps_type_tag_identity_matches(target, value);
}

static node_t *new_decl_initializer_assign(
    const initializer_lowering_context_t *context,
    node_t *target, node_t *value, token_t *tok) {
  node_t *assign = ps_node_new_assign_in(
      context->arena_context, target, value);
  assign->is_decl_initializer = 1;
  assign->tok = tok;
  return assign;
}

static node_t *lower_aggregate_expr_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer, int allow_union_scalar) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *target_type = var ? ps_lvar_get_decl_type(var) : NULL;
  const psx_type_t *value_type = ps_node_get_type(initializer->base.rhs);
  token_t *tok = initializer->base.tok;
  if (!var || !target_type) return NULL;

  if (aggregate_types_compatible(context, target_type, value_type)) {
    return new_decl_initializer_assign(
        context, initializer->base.lhs, initializer->base.rhs, tok);
  }
  if (!allow_union_scalar) {
    ps_diag_ctx_in(diagnostics(context), tok, "decl", "%s",
                 diag_message_for_in(diagnostics(context),
                     DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
  }

  const psx_record_decl_t *record = record_decl(context, target_type);
  if (!record) {
    ps_diag_ctx_in(diagnostics(context), tok, "decl", "%s",
                 diag_message_for_in(diagnostics(context),
                     DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *member = &record->members[i];
    if (member->len <= 0) continue;
    psx_initializer_member_ref_t member_ref = initializer_member_ref(
        context, target_type, i, member);
    node_t *target = new_initializer_member_lvar_ref(
        context, var, member_ref.layout.offset, &member_ref);
    return new_decl_initializer_assign(
        context, target, initializer->base.rhs, tok);
  }
  ps_diag_ctx_in(diagnostics(context), tok, "decl", "%s",
               diag_message_for_in(diagnostics(context),
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
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer) {
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (list->entry_count != 1 ||
      !initializer_entry_is_plain_scalar(&list->entries[0])) {
    ps_diag_ctx_in(diagnostics(context), initializer->base.tok, "decl", "%s",
                 diag_message_for_in(diagnostics(context),
                     DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
  }
  return new_decl_initializer_assign(
      context, initializer->base.lhs, list->entries[0].value,
      initializer->base.tok);
}

static node_t *lower_complex_list_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer, lvar_t *var) {
  node_init_list_t *list = (node_init_list_t *)initializer->base.rhs;
  if (list->entry_count < 1 || list->entry_count > 2) {
    ps_diag_ctx_in(diagnostics(context), initializer->base.tok, "decl", "%s",
                 diag_message_for_in(diagnostics(context),
                     DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
  }
  for (int i = 0; i < list->entry_count; i++) {
    if (!initializer_entry_is_plain_scalar(&list->entries[i])) {
      ps_diag_ctx_in(diagnostics(context), initializer->base.tok, "decl", "%s",
                   diag_message_for_in(diagnostics(context),
                       DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
    }
  }

  int complex_size = lvar_decl_size(context, var);
  if (complex_size <= 0)
    complex_size = lvar_storage_size(context, var, 0);
  int half = complex_size > 0 ? complex_size / 2 : 8;
  node_t *real_lhs = ps_node_new_lvar_fp_slot_for_in(
      context->arena_context, var, ps_lvar_offset(var), half);
  node_t *imag_lhs = ps_node_new_lvar_fp_slot_for_in(
      context->arena_context, var, ps_lvar_offset(var) + half, half);
  node_t *real_assign = new_decl_initializer_assign(
      context, real_lhs, list->entries[0].value, initializer->base.tok);
  node_t *imag_assign = new_decl_initializer_assign(
      context, imag_lhs,
      list->entry_count > 1
          ? list->entries[1].value
          : ps_node_new_num_in(context->arena_context, 0),
      initializer->base.tok);
  return ps_node_new_binary_for_target_in(
      context->arena_context, context->target,
      ND_COMMA, real_assign, imag_assign);
}

static node_t *lower_typed_list_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer) {
  if (!initializer || initializer->base.rhs->kind != ND_INIT_LIST)
    return NULL;
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  if (!var || !type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY)
    return lower_array_list_initializer(context, initializer);
  if (type->kind == PSX_TYPE_STRUCT)
    return lower_struct_list_initializer(context, initializer);
  if (type->kind == PSX_TYPE_UNION)
    return lower_union_list_initializer(context, initializer);
  if (type->kind == PSX_TYPE_COMPLEX)
    return lower_complex_list_initializer(context, initializer, var);
  return lower_scalar_list_initializer(context, initializer);
}

static node_t *lower_typed_expr_initializer(
    const initializer_lowering_context_t *context,
    node_decl_init_t *initializer) {
  lvar_t *var = ps_node_lvar_symbol(initializer->base.lhs);
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  if (!var || !type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    return lower_array_expr_initializer(
        context, initializer->base.lhs, initializer->base.rhs,
        initializer->base.tok);
  }
  if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION) {
    return lower_aggregate_expr_initializer(
        context, initializer, type->kind == PSX_TYPE_UNION);
  }
  return new_decl_initializer_assign(
      context, initializer->base.lhs, initializer->base.rhs,
      initializer->base.tok);
}

node_t *lower_decl_initializer(
    psx_lowering_context_t *lowering_context,
    node_t *node, const ag_compilation_options_t *options) {
  if (!node || node->kind != ND_DECL_INIT || !node->lhs || !node->rhs ||
      !options)
    return node;
  initializer_lowering_context_t context = {
      .arena_context = ps_lowering_arena(lowering_context),
      .diagnostic_context = ps_lowering_diagnostics(lowering_context),
      .options = options,
      .semantic_types = ps_lowering_semantic_types(lowering_context),
      .record_decls = ps_lowering_record_decls(lowering_context),
      .record_layouts = ps_lowering_record_layouts(lowering_context),
      .target = ps_lowering_target(lowering_context),
  };

  psx_decl_init_kind_t init_kind = ((node_decl_init_t *)node)->init_kind;
  token_t *tok = node->tok;
  if (init_kind == PSX_DECL_INIT_EXPR) {
    node_t *lowered = lower_typed_expr_initializer(
        &context, (node_decl_init_t *)node);
    if (!lowered) return node;
    if (!lowered->tok) lowered->tok = tok;
    return lowered;
  }

  if (init_kind == PSX_DECL_INIT_LIST) {
    node_t *lowered = lower_typed_list_initializer(
        &context, (node_decl_init_t *)node);
    if (!lowered) return node;
    if (!lowered->tok) lowered->tok = tok;
    return lowered;
  }

  return node;
}
