#include "resolution_work_tree.h"

#include <stddef.h>
#include <string.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/node_utils.h"

static size_t node_storage_size(const node_t *node) {
  switch (node->kind) {
    case ND_IDENTIFIER: return sizeof(node_identifier_t);
    case ND_COMPOUND_LITERAL: return sizeof(node_compound_literal_t);
    case ND_CAST:
      return node->is_source_cast ? sizeof(node_source_cast_t)
                                  : sizeof(node_t);
    case ND_MEMBER_ACCESS: return sizeof(node_member_access_t);
    case ND_GENERIC_SELECTION: return sizeof(node_generic_selection_t);
    case ND_SIZEOF_QUERY: return sizeof(node_sizeof_query_t);
    case ND_ALIGNOF_QUERY: return sizeof(node_alignof_query_t);
    case ND_INIT_LIST: return sizeof(node_init_list_t);
    case ND_DECL_INIT: return sizeof(node_decl_init_t);
    case ND_VLA_ALLOC: return sizeof(node_vla_alloc_t);
    case ND_NUM: return sizeof(node_num_t);
    case ND_LVAR: return sizeof(node_lvar_t);
    case ND_STRING: return sizeof(node_string_t);
    case ND_BLOCK: return sizeof(node_block_t);
    case ND_FUNCDEF: return sizeof(node_function_definition_t);
    case ND_FUNCALL: return sizeof(node_function_call_t);
    case ND_FUNCREF: return sizeof(node_funcref_t);
    case ND_IF:
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_FOR:
    case ND_SWITCH:
    case ND_TERNARY:
      return sizeof(node_ctrl_t);
    case ND_CASE: return sizeof(node_case_t);
    case ND_DEFAULT: return sizeof(node_default_t);
    case ND_GOTO:
    case ND_LABEL:
      return sizeof(node_jump_t);
    case ND_GVAR: return sizeof(node_gvar_t);
    default: return sizeof(node_t);
  }
}

static node_t *clone_node(
    arena_context_t *arena_context, const node_t *source);

static node_t **clone_node_array(
    arena_context_t *arena_context, node_t *const *source,
    size_t count, int null_terminated) {
  size_t slots = count + (null_terminated ? 1u : 0u);
  if (!source || slots == 0) return NULL;
  node_t **copy = arena_alloc_in(
      arena_context, slots * sizeof(*copy));
  if (!copy) return NULL;
  for (size_t i = 0; i < count; i++) {
    copy[i] = clone_node(arena_context, source[i]);
    if (source[i] && !copy[i]) return NULL;
  }
  if (null_terminated) copy[count] = NULL;
  return copy;
}

static node_t *cloned_designator_expression_alias(
    const psx_initializer_entry_t *source,
    psx_initializer_entry_t *destination,
    const node_t *expression) {
  if (!source || !destination || !expression) return NULL;
  for (int d = 0; d < source->designator_count; d++) {
    if (source->designators[d].index_expr == expression)
      return destination->designators[d].index_expr;
    if (source->designators[d].range_end_expr == expression)
      return destination->designators[d].range_end_expr;
  }
  return NULL;
}

static int clone_initializer_entries(
    arena_context_t *arena_context, node_init_list_t *destination,
    const node_init_list_t *source) {
  if (!source->entries || source->entry_count <= 0) {
    destination->entries = NULL;
    return 1;
  }
  size_t count = (size_t)source->entry_count;
  destination->entries = arena_alloc_in(
      arena_context, count * sizeof(*destination->entries));
  if (!destination->entries) return 0;
  memcpy(destination->entries, source->entries,
         count * sizeof(*destination->entries));
  for (size_t i = 0; i < count; i++) {
    const psx_initializer_entry_t *source_entry = &source->entries[i];
    psx_initializer_entry_t *entry = &destination->entries[i];
    entry->value = clone_node(arena_context, source_entry->value);
    if (source_entry->value && !entry->value) return 0;
    for (int d = 0; d < source_entry->designator_count; d++) {
      entry->designators[d].index_expr = clone_node(
          arena_context, source_entry->designators[d].index_expr);
      entry->designators[d].range_end_expr = clone_node(
          arena_context, source_entry->designators[d].range_end_expr);
      if ((source_entry->designators[d].index_expr &&
           !entry->designators[d].index_expr) ||
          (source_entry->designators[d].range_end_expr &&
           !entry->designators[d].range_end_expr)) {
        return 0;
      }
    }
    for (int d = 0; d < source_entry->index_expr_count; d++) {
      entry->index_exprs[d] = cloned_designator_expression_alias(
          source_entry, entry, source_entry->index_exprs[d]);
      if (!entry->index_exprs[d])
        entry->index_exprs[d] = clone_node(
            arena_context, source_entry->index_exprs[d]);
      if (source_entry->index_exprs[d] && !entry->index_exprs[d])
        return 0;
    }
  }
  return 1;
}

static int clone_generic_selection(
    arena_context_t *arena_context,
    node_generic_selection_t *destination,
    const node_generic_selection_t *source) {
  destination->control = clone_node(arena_context, source->control);
  if (source->control && !destination->control) return 0;
  if (!source->associations || source->association_count <= 0) {
    destination->associations = NULL;
    return 1;
  }
  size_t count = (size_t)source->association_count;
  destination->associations = arena_alloc_in(
      arena_context, count * sizeof(*destination->associations));
  if (!destination->associations) return 0;
  memcpy(destination->associations, source->associations,
         count * sizeof(*destination->associations));
  for (size_t i = 0; i < count; i++) {
    destination->associations[i].expression = clone_node(
        arena_context, source->associations[i].expression);
    if (source->associations[i].expression &&
        !destination->associations[i].expression) {
      return 0;
    }
  }
  return 1;
}

static node_t *clone_node(
    arena_context_t *arena_context, const node_t *source) {
  if (!source) return NULL;
  size_t size = node_storage_size(source);
  node_t *copy = arena_alloc_in(arena_context, size);
  if (!copy) return NULL;
  memcpy(copy, source, size);
  copy->resolution_state = NULL;
  if (!ps_node_prepare_resolution_state_in(
          arena_context, copy))
    return NULL;
  if (source->resolution_state &&
      !ps_node_copy_resolution_state_in(
          arena_context, copy, source))
    return NULL;
  copy->lhs = clone_node(arena_context, source->lhs);
  copy->rhs = source->kind == ND_STMT_EXPR
                  ? NULL
                  : clone_node(arena_context, source->rhs);
  if ((source->lhs && !copy->lhs) ||
      (source->kind != ND_STMT_EXPR && source->rhs && !copy->rhs))
    return NULL;

  switch (source->kind) {
    case ND_BLOCK: {
      const node_block_t *source_block = (const node_block_t *)source;
      node_block_t *block = (node_block_t *)copy;
      size_t count = 0;
      while (source_block->body && source_block->body[count]) count++;
      block->body = clone_node_array(
          arena_context, source_block->body, count, 1);
      if (source_block->body && !block->body) return NULL;
      break;
    }
    case ND_STMT_EXPR: {
      const node_block_t *source_block =
          source->lhs && source->lhs->kind == ND_BLOCK
              ? (const node_block_t *)source->lhs : NULL;
      node_block_t *block =
          copy->lhs && copy->lhs->kind == ND_BLOCK
              ? (node_block_t *)copy->lhs : NULL;
      if (source_block && block) {
        for (size_t i = 0;
             source_block->body && source_block->body[i]; i++) {
          if (source_block->body[i] == source->rhs) {
            copy->rhs = block->body[i];
            break;
          }
        }
      }
      if (!copy->rhs)
        copy->rhs = clone_node(arena_context, source->rhs);
      if (source->rhs && !copy->rhs) return NULL;
      break;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *source_function =
          (const node_function_definition_t *)source;
      node_function_definition_t *function =
          (node_function_definition_t *)copy;
      function->parameters = clone_node_array(
          arena_context, source_function->parameters,
          source_function->parameter_count > 0
              ? (size_t)source_function->parameter_count : 0,
          1);
      if (source_function->parameter_count > 0 &&
          !function->parameters)
        return NULL;
      break;
    }
    case ND_FUNCALL: {
      const node_function_call_t *source_call =
          (const node_function_call_t *)source;
      node_function_call_t *call = (node_function_call_t *)copy;
      call->callee = clone_node(arena_context, source_call->callee);
      if (source_call->callee && !call->callee) return NULL;
      call->arguments = clone_node_array(
          arena_context, source_call->arguments,
          source_call->argument_count > 0
              ? (size_t)source_call->argument_count : 0,
          1);
      if (source_call->argument_count > 0 && !call->arguments) return NULL;
      break;
    }
    case ND_IF:
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_FOR:
    case ND_SWITCH:
    case ND_TERNARY: {
      const node_ctrl_t *source_control = (const node_ctrl_t *)source;
      node_ctrl_t *control = (node_ctrl_t *)copy;
      control->init = clone_node(arena_context, source_control->init);
      control->inc = clone_node(arena_context, source_control->inc);
      control->els = clone_node(arena_context, source_control->els);
      if ((source_control->init && !control->init) ||
          (source_control->inc && !control->inc) ||
          (source_control->els && !control->els)) {
        return NULL;
      }
      break;
    }
    case ND_GENERIC_SELECTION:
      if (!clone_generic_selection(
              arena_context, (node_generic_selection_t *)copy,
              (const node_generic_selection_t *)source)) {
        return NULL;
      }
      break;
    case ND_SIZEOF_QUERY: {
      const node_sizeof_query_t *source_query =
          (const node_sizeof_query_t *)source;
      node_sizeof_query_t *query = (node_sizeof_query_t *)copy;
      query->operand = clone_node(arena_context, source_query->operand);
      query->runtime_size_expr = clone_node(
          arena_context, source_query->runtime_size_expr);
      if ((source_query->operand && !query->operand) ||
          (source_query->runtime_size_expr && !query->runtime_size_expr)) {
        return NULL;
      }
      break;
    }
    case ND_INIT_LIST:
      if (!clone_initializer_entries(
              arena_context, (node_init_list_t *)copy,
              (const node_init_list_t *)source)) {
        return NULL;
      }
      break;
    default:
      break;
  }
  return copy;
}

node_t *psx_clone_syntax_tree_for_resolution(
    arena_context_t *arena_context, const node_t *syntax_root) {
  return arena_context && syntax_root
             ? clone_node(arena_context, syntax_root) : NULL;
}
