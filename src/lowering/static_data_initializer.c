#include "static_data_initializer.h"
#include "static_hir_initializer.h"
#include "runtime_context.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../semantic/resolution_state.h"
#include "../parser/node_utils.h"
#include "../semantic/compound_literal_resolution.h"
#include "../semantic/constant_expression.h"
#include "../semantic/initializer_resolution.h"
#include "../semantic/literal_resolution.h"
#include "../semantic/member_access_resolution.h"
#include "../semantic/resolved_node_kind.h"
#include "../semantic/resolved_object_ref.h"
#include "../semantic/static_initializer_resolution.h"
#include "../tokenizer/literals.h"
#include "../type_layout.h"

#include <string.h>

typedef struct {
  psx_lowering_context_t *lowering_context;
  global_var_t *global;
  psx_initializer_scalar_leaf_list_t leaves;
  token_t *fallback_tok;
} static_array_lowering_t;

static psx_resolution_store_t *resolution_store(
    const psx_lowering_context_t *lowering_context) {
  return ps_lowering_resolution_store(lowering_context);
}

static ag_diagnostic_context_t *diagnostics(
    const static_array_lowering_t *lowering) {
  return ps_lowering_diagnostics(lowering->lowering_context);
}

static int lowering_type_size(
    const psx_lowering_context_t *lowering_context,
    const psx_type_t *type) {
  return ps_type_sizeof_id(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_layouts(lowering_context),
      ps_lowering_type_id(lowering_context, type),
      ag_target_info_data_layout(ps_lowering_target(lowering_context)));
}

static int type_size(
    const static_array_lowering_t *lowering, const psx_type_t *type) {
  return lowering_type_size(lowering->lowering_context, type);
}

static const psx_type_t *type_view(
    const static_array_lowering_t *lowering, psx_type_id_t type_id) {
  return lowering
             ? psx_semantic_type_table_lookup(
                   ps_lowering_semantic_types(
                       lowering->lowering_context), type_id)
             : NULL;
}

static int resolved_member_offset(
    psx_lowering_context_t *lowering_context,
    const node_member_access_t *access, int *offset) {
  const psx_member_access_state_t *state =
      psx_member_access_state(
          resolution_store(lowering_context), access);
  if (!lowering_context || !access || !offset ||
      !state || !state->is_resolved ||
      state->record_id == PSX_RECORD_ID_INVALID ||
      state->member_index < 0)
    return 0;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      ps_lowering_record_layouts(lowering_context), state->record_id,
      ag_target_info_data_layout(ps_lowering_target(lowering_context)));
  const psx_record_member_layout_t *layout_member =
      psx_record_layout_member(layout, state->member_index);
  if (!layout_member) return 0;
  *offset = layout_member->offset;
  return 1;
}

static int static_pointer_stride(
    psx_lowering_context_t *lowering_context, const node_t *pointer) {
  if (!lowering_context || !pointer) return 0;
  psx_type_id_t pointer_type_id =
      ps_node_qual_type(
          resolution_store(lowering_context), pointer).type_id;
  if (pointer_type_id == PSX_TYPE_ID_INVALID)
    pointer_type_id = ps_lowering_type_id(
        lowering_context, ps_node_get_type(
                              resolution_store(lowering_context),
                              (node_t *)pointer));
  psx_qual_type_t element_type = psx_semantic_type_table_base(
      ps_lowering_semantic_types(lowering_context), pointer_type_id);
  if (element_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  return ps_type_sizeof_id(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_layouts(lowering_context), element_type.type_id,
      ag_target_info_data_layout(ps_lowering_target(lowering_context)));
}

static int resolve_static_address_constant(
    psx_lowering_context_t *lowering_context, node_t *node,
    char **symbol, int *symbol_len, long long *offset) {
  if (!lowering_context || !node || !symbol || !symbol_len || !offset)
    return 0;
  switch (psx_resolution_node_kind(
      resolution_store(lowering_context), node)) {
    case ND_COMPOUND_LITERAL: {
      const psx_node_resolution_state_t *state =
          ps_node_resolution_state_const(
              resolution_store(lowering_context), node);
      const psx_compound_literal_resolution_t *resolution =
          state ? &state->compound_literal : NULL;
      if (!resolution ||
          resolution->kind != PSX_COMPOUND_LITERAL_GLOBAL_OBJECT ||
          !resolution->global_object)
        return 0;
      *symbol = ps_gvar_name(resolution->global_object);
      *symbol_len = ps_gvar_name_len(resolution->global_object);
      return *symbol != NULL && *symbol_len > 0;
    }
    case ND_ADDRESS_OF:
    case ND_ADDR:
      if (node->lhs &&
          (node->lhs->kind == ND_SUBSCRIPT ||
           node->lhs->kind == ND_MEMBER_ACCESS ||
           psx_resolution_node_kind(
               resolution_store(lowering_context), node->lhs) == ND_DEREF))
        return resolve_static_address_constant(
            lowering_context,
            psx_resolution_node_kind(
                resolution_store(lowering_context), node->lhs) == ND_DEREF
                ? node->lhs->lhs : node->lhs,
            symbol, symbol_len, offset);
      break;
    case ND_SUBSCRIPT: {
      if (!resolve_static_address_constant(
              lowering_context, node->lhs,
              symbol, symbol_len, offset))
        return 0;
      int ok = 1;
      long long index = psx_eval_const_int(
          resolution_store(lowering_context), node->rhs, &ok);
      psx_type_id_t base_type_id =
          ps_node_qual_type(
              resolution_store(lowering_context), node->lhs).type_id;
      if (base_type_id == PSX_TYPE_ID_INVALID)
        base_type_id = ps_lowering_type_id(
            lowering_context, ps_node_get_type(
                                  resolution_store(lowering_context),
                                  node->lhs));
      psx_qual_type_t element_type =
          psx_semantic_type_table_base(
              ps_lowering_semantic_types(lowering_context),
              base_type_id);
      int stride = ps_type_sizeof_id(
          ps_lowering_semantic_types(lowering_context),
          ps_lowering_record_layouts(lowering_context), element_type.type_id,
          ag_target_info_data_layout(ps_lowering_target(lowering_context)));
      if (!ok || stride <= 0) return 0;
      *offset += index * stride;
      return 1;
    }
    case ND_MEMBER_ACCESS: {
      const node_member_access_t *access =
          (const node_member_access_t *)node;
      int member_offset = 0;
      if (!resolve_static_address_constant(
              lowering_context, node->lhs,
              symbol, symbol_len, offset) ||
          !resolved_member_offset(
              lowering_context, access, &member_offset))
        return 0;
      *offset += member_offset;
      return 1;
    }
    case ND_CAST: {
      return resolve_static_address_constant(
          lowering_context, node->lhs,
          symbol, symbol_len, offset);
    }
    case ND_ADD: {
      int ok = 1;
      if (resolve_static_address_constant(
              lowering_context, node->lhs,
              symbol, symbol_len, offset)) {
        long long addend = psx_eval_const_int(
            resolution_store(lowering_context), node->rhs, &ok);
        if (!ok) return 0;
        if (node->source_op == TK_PLUS) {
          int stride = static_pointer_stride(
              lowering_context, node->lhs);
          if (stride <= 0) return 0;
          addend *= stride;
        }
        *offset += addend;
        return 1;
      }
      if (resolve_static_address_constant(
              lowering_context, node->rhs,
              symbol, symbol_len, offset)) {
        long long addend = psx_eval_const_int(
            resolution_store(lowering_context), node->lhs, &ok);
        if (!ok) return 0;
        if (node->source_op == TK_PLUS) {
          int stride = static_pointer_stride(
              lowering_context, node->rhs);
          if (stride <= 0) return 0;
          addend *= stride;
        }
        *offset += addend;
        return 1;
      }
      return 0;
    }
    case ND_SUB: {
      int ok = 1;
      if (!resolve_static_address_constant(
              lowering_context, node->lhs,
              symbol, symbol_len, offset))
        return 0;
      long long addend = psx_eval_const_int(
          resolution_store(lowering_context), node->rhs, &ok);
      if (!ok) return 0;
      if (node->source_op == TK_MINUS) {
        int stride = static_pointer_stride(
            lowering_context, node->lhs);
        if (stride <= 0) return 0;
        addend *= stride;
      }
      *offset -= addend;
      return 1;
    }
    default:
      break;
  }
  return psx_resolve_static_address_constant(
      resolution_store(lowering_context),
      node, symbol, symbol_len, offset);
}

static int same_static_symbol(
    const char *left, int left_len, const char *right, int right_len) {
  if (!left || !right || left_len != right_len) return 0;
  if (left_len == -1) return left == right;
  return left_len > 0 &&
         memcmp(left, right, (size_t)left_len) == 0;
}

static long long eval_static_const_int(
    psx_lowering_context_t *lowering_context,
    node_t *node, int *ok) {
  int direct_ok = 1;
  long long direct = psx_eval_const_int(
      resolution_store(lowering_context), node, &direct_ok);
  if (direct_ok) {
    if (ok) *ok = 1;
    return direct;
  }
  if (!node) {
    if (ok) *ok = 0;
    return 0;
  }
  if (node->kind == ND_COMPOUND_LITERAL) {
    node_t *direct = psx_compound_literal_direct_initializer(
        resolution_store(lowering_context),
        (node_compound_literal_t *)node);
    if (direct) {
      return eval_static_const_int(
          lowering_context, direct, ok);
    }
  }
  if (psx_resolution_node_kind(
          resolution_store(lowering_context), node) == ND_CAST) {
    return eval_static_const_int(
        lowering_context, node->lhs, ok);
  }
  if (node->kind == ND_UNARY_PLUS)
    return eval_static_const_int(
        lowering_context, node->lhs, ok);
  if (node->kind == ND_UNARY_NEGATE) {
    long long value =
        eval_static_const_int(lowering_context, node->lhs, ok);
    return !ok || *ok ? -value : 0;
  }
  if (node->kind == ND_LOGICAL_NOT) {
    long long value =
        eval_static_const_int(lowering_context, node->lhs, ok);
    return !ok || *ok ? !value : 0;
  }
  if (node->kind == ND_BITWISE_NOT) {
    long long value =
        eval_static_const_int(lowering_context, node->lhs, ok);
    return !ok || *ok ? ~value : 0;
  }
  if (node->kind == ND_SUB) {
    char *left_symbol = NULL;
    char *right_symbol = NULL;
    int left_len = 0;
    int right_len = 0;
    long long left_offset = 0;
    long long right_offset = 0;
    if (resolve_static_address_constant(
            lowering_context, node->lhs,
            &left_symbol, &left_len, &left_offset) &&
        resolve_static_address_constant(
            lowering_context, node->rhs,
            &right_symbol, &right_len, &right_offset) &&
        same_static_symbol(
            left_symbol, left_len, right_symbol, right_len)) {
      long long difference = left_offset - right_offset;
      if (node->source_op == TK_MINUS) {
        int stride = static_pointer_stride(
            lowering_context, node->lhs);
        if (stride <= 0 || difference % stride != 0) {
          if (ok) *ok = 0;
          return 0;
        }
        difference /= stride;
      }
      if (ok) *ok = 1;
      return difference;
    }
  }
  switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_SHL:
    case ND_SHR:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_GT:
    case ND_GE:
    case ND_LOGAND:
    case ND_LOGOR:
      break;
    default:
      if (ok) *ok = 0;
      return 0;
  }
  int operands_ok = 1;
  long long left = eval_static_const_int(
      lowering_context, node->lhs, &operands_ok);
  if (!operands_ok) {
    if (ok) *ok = 0;
    return 0;
  }
  long long right = eval_static_const_int(
      lowering_context, node->rhs, &operands_ok);
  if (!operands_ok ||
      ((node->kind == ND_DIV || node->kind == ND_MOD) && right == 0)) {
    if (ok) *ok = 0;
    return 0;
  }
  if (ok) *ok = 1;
  switch (node->kind) {
    case ND_ADD: return left + right;
    case ND_SUB: return left - right;
    case ND_MUL: return left * right;
    case ND_DIV: return left / right;
    case ND_MOD: return left % right;
    case ND_SHL: return left << right;
    case ND_SHR: return left >> right;
    case ND_BITAND: return left & right;
    case ND_BITXOR: return left ^ right;
    case ND_BITOR: return left | right;
    case ND_EQ: return left == right;
    case ND_NE: return left != right;
    case ND_LT: return left < right;
    case ND_LE: return left <= right;
    case ND_GT: return left > right;
    case ND_GE: return left >= right;
    case ND_LOGAND: return left && right;
    case ND_LOGOR: return left || right;
    default:
      if (ok) *ok = 0;
      return 0;
  }
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
      ag_target_info_data_layout(ps_lowering_target(context)));
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
    const psx_record_member_decl_t *declaration) {
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
  target.type_id = leaf->type_id;
  target.relative_offset = leaf->relative_offset;
  target.member_ref = leaf->member_ref;
  const psx_record_decl_t *record = record_decl(
      lowering->lowering_context, context_type);
  if (context_type->kind == PSX_TYPE_UNION &&
      record && record->member_count > 0) {
    const psx_record_member_decl_t *member = &record->members[0];
    if (preserve_subobject) {
      target.type_id = ps_lowering_type_id(
          lowering->lowering_context,
          psx_record_member_decl_type(
              ps_lowering_semantic_types(lowering->lowering_context),
              member));
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
      const psx_record_member_decl_t *member = &record->members[i];
      const psx_type_t *member_type = psx_record_member_decl_type(
          ps_lowering_semantic_types(lowering->lowering_context), member);
      int member_offset = context_offset +
                          record_member_offset(
                              lowering, context_type, i);
      if (member_offset != leaf->relative_offset || !member_type) continue;
      if (member_type->kind != PSX_TYPE_ARRAY &&
          !ps_type_is_tag_aggregate(member_type)) continue;
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
  target.type_id = ps_lowering_type_id(
      lowering->lowering_context, context_type->base);
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
  const psx_type_t *type = type_view(lowering, target->type_id);
  char *symbol = NULL;
  int symbol_len = 0;
  long long integer = 0;
  double floating = 0.0;
  long long offset = 0;
  if (type && (type->kind == PSX_TYPE_POINTER ||
               type->kind == PSX_TYPE_FUNCTION)) {
    if (resolve_static_address_constant(
            lowering->lowering_context, value,
            &symbol, &symbol_len, &offset)) {
      integer = offset;
    }
  } else if (type && type->kind == PSX_TYPE_FLOAT) {
    int ok = 1;
    floating = psx_eval_const_fp(
        resolution_store(lowering->lowering_context), value, &ok);
    if (!ok) floating = 0.0;
  } else {
    int ok = 1;
    integer = eval_static_const_int(
        lowering->lowering_context, value, &ok);
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
    psx_initializer_target_t target =
        entry->designator_count > 0
            ? psx_resolve_initializer_designator_path_with_records(
                  resolution_store(lowering->lowering_context),
                  diagnostics(lowering),
                  ps_lowering_semantic_types(lowering->lowering_context),
                  ps_lowering_record_decls(lowering->lowering_context),
                  ps_lowering_record_layouts(lowering->lowering_context),
                  ps_lowering_data_layout(lowering->lowering_context), entry,
                  ps_lowering_type_id(lowering->lowering_context, context_type),
                  context_offset, tok)
            : positional_target(
                  lowering, context_type, context_offset, &lowering->leaves,
                  cursor, entry->value && entry->value->kind == ND_INIT_LIST);
    const psx_type_t *target_type = type_view(lowering, target.type_id);
    if (!target_type) {
      ps_diag_ctx_in(
          diagnostics(lowering), tok, "static-init", "%s",
          diag_message_for_in(
              diagnostics(lowering),
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
    }
    mark_union_target(lowering, &target);

    if (entry->value && entry->value->kind == ND_INIT_LIST) {
      lower_array_list(
          lowering, target_type, target.relative_offset,
          (node_init_list_t *)entry->value);
    } else if (entry->value && entry->value->kind == ND_STRING &&
               (is_character_array_for_string(
                    lowering, target_type,
                    (node_string_t *)entry->value) ||
                (cursor >= 0 && cursor < lowering->leaves.count &&
                 is_character_array_for_string(
                     lowering,
                     type_view(
                         lowering,
                         lowering->leaves.items[cursor]
                             .string_array_type_id),
                     (node_string_t *)entry->value)))) {
      const psx_type_t *string_type = target_type;
      int string_offset = target.relative_offset;
      if (string_type->kind != PSX_TYPE_ARRAY && cursor >= 0 &&
          cursor < lowering->leaves.count) {
        psx_initializer_scalar_leaf_t *leaf = &lowering->leaves.items[cursor];
        if (leaf->string_array_type_id != PSX_TYPE_ID_INVALID) {
          string_type = type_view(
              lowering, leaf->string_array_type_id);
          string_offset = leaf->string_array_offset;
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
        ps_lowering_data_layout(lowering->lowering_context), &lowering->leaves,
        &target);
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
              psx_record_member_decl_type(
                  ps_lowering_semantic_types(lowering_context),
                  &record->members[i])))
        return 1;
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
          ps_lowering_data_layout(lowering_context),
          ps_gvar_decl_type_id(global), 0, &lowering.leaves) ||
      lowering.leaves.count <= 0) {
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

int psx_build_static_aggregate_initializer_plan(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    const psx_type_t *type, node_init_list_t *initializer,
    token_t *fallback_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  if (!global_registry || !lowering_context || !type ||
      !initializer || !plan)
    return 0;
  global_var_t temporary = {0};
  if (!ps_global_registry_bind_decl_type(
          global_registry, &temporary, type) ||
      !lower_static_object_initializer(
          lowering_context, &temporary, type,
          initializer, fallback_tok))
    return 0;
  *plan = (psx_static_aggregate_initializer_plan_t){
      .values = temporary.init_values,
      .floating_values = temporary.init_fvalues,
      .symbols = temporary.init_value_symbols,
      .symbol_lengths = temporary.init_value_symbol_lens,
      .union_ordinals = temporary.init_union_ordinals,
      .value_count = temporary.init_count,
      .union_ordinal = temporary.union_init_ordinal,
  };
  return plan->value_count > 0;
}

int psx_apply_static_aggregate_initializer_plan(
    global_var_t *global,
    const psx_static_aggregate_initializer_plan_t *plan) {
  if (!global || !plan || plan->value_count <= 0 ||
      !plan->values || !plan->symbols ||
      !plan->symbol_lengths || !plan->union_ordinals)
    return 0;
  global->init_values = plan->values;
  global->init_fvalues = plan->floating_values;
  global->init_value_symbols = plan->symbols;
  global->init_value_symbol_lens = plan->symbol_lengths;
  global->init_union_ordinals = plan->union_ordinals;
  global->init_count = plan->value_count;
  global->union_init_ordinal = plan->union_ordinal;
  return 1;
}

static int lower_static_string_expression(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type, node_string_t *string) {
  if (!global || !type || !string) return 0;
  if (type->kind == PSX_TYPE_POINTER) {
    global->init_symbol = psx_string_literal_label(
        resolution_store(lowering_context), string);
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
  long long integer = eval_static_const_int(
      lowering_context, initializer, &integer_ok);
  if (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX) {
    int floating_ok = 1;
    double floating = psx_eval_const_fp(
        resolution_store(lowering_context), initializer, &floating_ok);
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
  if (resolve_static_address_constant(
          lowering_context, initializer,
          &symbol, &symbol_len, &offset)) {
    global->init_symbol = symbol;
    global->init_symbol_len = symbol_len;
    global->init_symbol_offset = offset;
    return 1;
  }
  if (psx_resolved_object_ref_node_kind(
          resolution_store(lowering_context), initializer) == ND_FUNCREF) {
    global->init_symbol = psx_resolved_object_ref_name(
        resolution_store(lowering_context), initializer,
        &global->init_symbol_len);
    return global->init_symbol != NULL;
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
      resolution->object_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !resolution->initializer)
    return 0;
  if (!psx_global_registry_note_global_mutation(
          global_registry, global))
    return 0;

  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      ps_lowering_semantic_types(lowering_context),
      resolution->object_qual_type);
  if (!type) return 0;
  if (resolution->type_completed) {
    if (!ps_global_registry_complete_array_type(
            global_registry, global, type)) return 0;
    if (result) result->type_completed = 1;
  }

  if (resolution->is_aggregate_initializer) {
    if (resolution->aggregate_plan) {
      if (!psx_apply_static_aggregate_initializer_plan(
              global, resolution->aggregate_plan))
        return 0;
    } else if (!lower_static_object_initializer(
                   lowering_context, global, type,
                   (node_init_list_t *)resolution->initializer, tok)) {
      return 0;
    }
    global->has_init = 1;
    if (result) result->initialized = 1;
    return 1;
  }

  if (resolution->initializer_hir) {
    if (!psx_lower_static_scalar_hir_initializer(
            global_registry, lowering_context, global,
            ps_gvar_decl_type_id(global),
            resolution->initializer_hir,
            resolution->initializer_hir_root))
      return 0;
  } else if (!lower_static_scalar_expression(
                 lowering_context, global, type,
                 resolution->initializer)) {
    return 0;
  }
  global->has_init = 1;
  if (result) result->initialized = 1;
  return 1;
}
