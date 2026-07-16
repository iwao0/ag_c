#include "resolution_work_tree.h"

#include <stddef.h>
#include <string.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/declaration_syntax.h"
#include "../parser/function_parameter_syntax.h"
#include "../parser/node_utils.h"
#include "vla_runtime_plan.h"

struct psx_resolution_work_tree_t {
  const node_t *syntax_root;
  node_t *semantic_root;
  psx_resolution_work_phase_t phase;
};

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
    case ND_STATIC_ASSERT: return sizeof(node_static_assert_t);
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

static psx_vla_runtime_plan_t *clone_vla_runtime_plan(
    arena_context_t *arena_context,
    const psx_vla_runtime_plan_t *source) {
  if (!source) return NULL;
  psx_vla_runtime_plan_t *copy = arena_alloc_in(
      arena_context, sizeof(*copy));
  if (!copy) return NULL;
  *copy = *source;
  copy->dimensions = NULL;
  copy->stride_store_offsets = NULL;
  copy->stride_start_dimensions = NULL;
  if (source->dimension_count > 0) {
    copy->dimensions = arena_alloc_in(
        arena_context,
        (size_t)source->dimension_count * sizeof(*copy->dimensions));
    if (!copy->dimensions) return NULL;
    for (int i = 0; i < source->dimension_count; i++) {
      copy->dimensions[i] = clone_node(
          arena_context, source->dimensions[i]);
      if (source->dimensions[i] && !copy->dimensions[i])
        return NULL;
    }
  }
  if (source->stride_store_count > 0) {
    size_t bytes =
        (size_t)source->stride_store_count * sizeof(int);
    copy->stride_store_offsets = arena_alloc_in(
        arena_context, bytes);
    copy->stride_start_dimensions = arena_alloc_in(
        arena_context, bytes);
    if (!copy->stride_store_offsets ||
        !copy->stride_start_dimensions)
      return NULL;
    memcpy(
        copy->stride_store_offsets,
        source->stride_store_offsets, bytes);
    memcpy(
        copy->stride_start_dimensions,
        source->stride_start_dimensions, bytes);
  }
  return copy;
}

static int clone_parsed_declarator(
    arena_context_t *arena_context,
    psx_parsed_declarator_t *destination,
    const psx_parsed_declarator_t *source);

static int clone_parsed_const_expr(
    arena_context_t *arena_context,
    psx_parsed_const_expr_t *destination,
    const psx_parsed_const_expr_t *source) {
  *destination = *source;
  destination->node = clone_node(arena_context, source->node);
  return !source->node || destination->node;
}

static int clone_decl_specifier(
    arena_context_t *arena_context,
    psx_parsed_decl_specifier_t *destination,
    const psx_parsed_decl_specifier_t *source) {
  *destination = *source;
  for (int i = 0; i < source->alignas_expression_count; i++) {
    if (!clone_parsed_const_expr(
            arena_context, &destination->alignas_expressions[i],
            &source->alignas_expressions[i]))
      return 0;
  }
  return 1;
}

static int clone_declarator_shape(
    arena_context_t *arena_context,
    psx_declarator_shape_t *destination,
    const psx_declarator_shape_t *source) {
  *destination = *source;
  destination->ops = NULL;
  destination->capacity = source->count;
  if (!source->ops || source->count <= 0) return 1;
  destination->ops = arena_alloc_in(
      arena_context, (size_t)source->count * sizeof(*destination->ops));
  if (!destination->ops) return 0;
  memcpy(
      destination->ops, source->ops,
      (size_t)source->count * sizeof(*destination->ops));
  for (int i = 0; i < source->count; i++) {
    const psx_declarator_op_t *source_op = &source->ops[i];
    psx_declarator_op_t *op = &destination->ops[i];
    if (!source_op->function_param_types ||
        source_op->function_param_count <= 0) {
      op->function_param_types = NULL;
      continue;
    }
    op->function_param_types = arena_alloc_in(
        arena_context,
        (size_t)source_op->function_param_count *
            sizeof(*op->function_param_types));
    if (!op->function_param_types) return 0;
    memcpy(
        op->function_param_types, source_op->function_param_types,
        (size_t)source_op->function_param_count *
            sizeof(*op->function_param_types));
  }
  return 1;
}

static psx_parsed_function_parameters_t *clone_function_parameters(
    arena_context_t *arena_context,
    const psx_parsed_function_parameters_t *source) {
  if (!source) return NULL;
  psx_parsed_function_parameters_t *destination = arena_alloc_in(
      arena_context, sizeof(*destination));
  if (!destination) return NULL;
  *destination = *source;
  destination->items = NULL;
  destination->capacity = source->count;
  if (!source->items || source->count <= 0) return destination;
  destination->items = arena_alloc_in(
      arena_context,
      (size_t)source->count * sizeof(*destination->items));
  if (!destination->items) return NULL;
  for (int i = 0; i < source->count; i++) {
    const psx_parsed_function_parameter_t *source_parameter =
        &source->items[i];
    psx_parsed_function_parameter_t *parameter =
        &destination->items[i];
    if (!clone_decl_specifier(
            arena_context, &parameter->specifier,
            &source_parameter->specifier) ||
        !clone_parsed_declarator(
            arena_context, &parameter->declarator,
            &source_parameter->declarator))
      return NULL;
  }
  return destination;
}

static int clone_parsed_declarator(
    arena_context_t *arena_context,
    psx_parsed_declarator_t *destination,
    const psx_parsed_declarator_t *source) {
  *destination = *source;
  if (!clone_declarator_shape(
          arena_context, &destination->declarator_shape,
          &source->declarator_shape) ||
      !clone_parsed_const_expr(
          arena_context, &destination->bit_width_expression,
          &source->bit_width_expression))
    return 0;

  destination->array_bounds = NULL;
  destination->array_bound_capacity = source->array_bound_count;
  if (source->array_bounds && source->array_bound_count > 0) {
    destination->array_bounds = arena_alloc_in(
        arena_context,
        (size_t)source->array_bound_count *
            sizeof(*destination->array_bounds));
    if (!destination->array_bounds) return 0;
    for (int i = 0; i < source->array_bound_count; i++) {
      destination->array_bounds[i] = source->array_bounds[i];
      if (!clone_parsed_const_expr(
              arena_context,
              &destination->array_bounds[i].expression,
              &source->array_bounds[i].expression))
        return 0;
    }
  }

  destination->function_suffixes = NULL;
  destination->function_suffix_capacity = source->function_suffix_count;
  if (source->function_suffixes && source->function_suffix_count > 0) {
    destination->function_suffixes = arena_alloc_in(
        arena_context,
        (size_t)source->function_suffix_count *
            sizeof(*destination->function_suffixes));
    if (!destination->function_suffixes) return 0;
    for (int i = 0; i < source->function_suffix_count; i++) {
      destination->function_suffixes[i] =
          source->function_suffixes[i];
      destination->function_suffixes[i].parameters =
          clone_function_parameters(
              arena_context,
              source->function_suffixes[i].parameters);
      if (source->function_suffixes[i].parameters &&
          !destination->function_suffixes[i].parameters)
        return 0;
    }
  }
  return 1;
}

static psx_parsed_type_name_t *clone_type_name_syntax(
    arena_context_t *arena_context,
    const psx_parsed_type_name_t *source) {
  if (!source) return NULL;
  psx_parsed_type_name_t *destination = arena_alloc_in(
      arena_context, sizeof(*destination));
  if (!destination) return NULL;
  *destination = *source;
  destination->atomic_inner = clone_type_name_syntax(
      arena_context, source->atomic_inner);
  if ((source->atomic_inner && !destination->atomic_inner) ||
      !clone_decl_specifier(
          arena_context, &destination->specifier,
          &source->specifier) ||
      !clone_parsed_declarator(
          arena_context, &destination->declarator,
          &source->declarator))
    return NULL;
  return destination;
}

static int clone_type_name_ref(
    arena_context_t *arena_context,
    psx_type_name_ref_t *destination,
    const psx_type_name_ref_t *source) {
  *destination = *source;
  destination->syntax = clone_type_name_syntax(
      arena_context, source->syntax);
  return !source->syntax || destination->syntax;
}

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
    if (!clone_type_name_ref(
            arena_context,
            &destination->associations[i].type_name,
            &source->associations[i].type_name))
      return 0;
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
    case ND_COMPOUND_LITERAL:
      if (!clone_type_name_ref(
              arena_context,
              &((node_compound_literal_t *)copy)->type_name,
              &((const node_compound_literal_t *)source)->type_name))
        return NULL;
      break;
    case ND_STATIC_ASSERT: {
      node_static_assert_t *assertion = (node_static_assert_t *)copy;
      const node_static_assert_t *source_assertion =
          (const node_static_assert_t *)source;
      assertion->condition = clone_node(
          arena_context, source_assertion->condition);
      if (source_assertion->condition && !assertion->condition)
        return NULL;
      break;
    }
    case ND_VLA_ALLOC: {
      node_vla_alloc_t *allocation = (node_vla_alloc_t *)copy;
      const node_vla_alloc_t *source_allocation =
          (const node_vla_alloc_t *)source;
      allocation->runtime_plan = clone_vla_runtime_plan(
          arena_context, source_allocation->runtime_plan);
      if (source_allocation->runtime_plan &&
          !allocation->runtime_plan)
        return NULL;
      break;
    }
    case ND_CAST:
      if (source->is_source_cast &&
          !clone_type_name_ref(
              arena_context,
              &((node_source_cast_t *)copy)->type_name,
              &((const node_source_cast_t *)source)->type_name))
        return NULL;
      break;
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
      if (!clone_type_name_ref(
              arena_context, &query->type_name,
              &source_query->type_name))
        return NULL;
      query->operand = clone_node(arena_context, source_query->operand);
      if (source_query->operand && !query->operand) {
        return NULL;
      }
      break;
    }
    case ND_ALIGNOF_QUERY:
      if (!clone_type_name_ref(
              arena_context,
              &((node_alignof_query_t *)copy)->type_name,
              &((const node_alignof_query_t *)source)->type_name))
        return NULL;
      break;
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

psx_resolution_work_tree_t *psx_resolution_work_tree_create_from_syntax(
    arena_context_t *arena_context, const node_t *syntax_root) {
  if (!arena_context || !syntax_root) return NULL;
  psx_resolution_work_tree_t *tree = arena_alloc_in(
      arena_context, sizeof(*tree));
  if (!tree) return NULL;
  tree->syntax_root = syntax_root;
  tree->semantic_root = clone_node(arena_context, syntax_root);
  if (!tree->semantic_root) return NULL;
  tree->phase = PSX_RESOLUTION_WORK_CLONED;
  return tree;
}

const node_t *psx_resolution_work_tree_syntax_root(
    const psx_resolution_work_tree_t *tree) {
  return tree ? tree->syntax_root : NULL;
}

node_t *psx_resolution_work_tree_mutable_semantic_root(
    psx_resolution_work_tree_t *tree) {
  return tree ? tree->semantic_root : NULL;
}

const node_t *psx_resolution_work_tree_semantic_root(
    const psx_resolution_work_tree_t *tree) {
  return tree ? tree->semantic_root : NULL;
}

node_t *psx_resolution_work_tree_legacy_root(
    psx_resolution_work_tree_t *tree) {
  return tree && tree->phase >= PSX_RESOLUTION_WORK_FINALIZED
             ? tree->semantic_root : NULL;
}

psx_resolution_work_phase_t psx_resolution_work_tree_phase(
    const psx_resolution_work_tree_t *tree) {
  return tree ? tree->phase : PSX_RESOLUTION_WORK_INVALID;
}

int psx_resolution_work_tree_advance_with_root(
    psx_resolution_work_tree_t *tree,
    psx_resolution_work_phase_t expected,
    psx_resolution_work_phase_t next, node_t *root) {
  if (!tree || !root || tree->phase != expected ||
      root == tree->syntax_root ||
      next != (psx_resolution_work_phase_t)(expected + 1))
    return 0;
  tree->semantic_root = root;
  tree->phase = next;
  return 1;
}
