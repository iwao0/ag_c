#include "assignment_lowering.h"
#include "local_storage.h"
#include "runtime_context.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static psx_syntax_node_kind_t compound_binary_kind(token_kind_t op) {
  switch (op) {
    case TK_PLUSEQ: return ND_ADD;
    case TK_MINUSEQ: return ND_SUB;
    case TK_MULEQ: return ND_MUL;
    case TK_DIVEQ: return ND_DIV;
    case TK_MODEQ: return ND_MOD;
    case TK_SHLEQ: return ND_SHL;
    case TK_SHREQ: return ND_SHR;
    case TK_ANDEQ: return ND_BITAND;
    case TK_XOREQ: return ND_BITXOR;
    case TK_OREQ: return ND_BITOR;
    default: return ND_ADD;
  }
}

static char *new_compound_assignment_temp_name(
    psx_lowering_context_t *lowering_context) {
  int seq = lowering_context->compound_assignment_temp_sequence++;
  int len = snprintf(NULL, 0, "__compound_assign_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__compound_assign_%d", seq);
  return name;
}

static node_t *materialize_lvalue_address_once(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_t *target, node_t **prefix) {
  if (!target ||
      (target->kind != ND_UNARY_DEREF &&
       target->kind != ND_DEREF) ||
      !target->lhs)
    return target;
  node_t *address = target->lhs;
  const psx_type_t *address_type = ps_node_get_type(address);
  if (!address_type) return target;
  char *name = new_compound_assignment_temp_name(lowering_context);
  int offset = local_storage_allocate(lowering_context, 8, 0);
  lvar_t *temp = ps_local_registry_create_storage_object_in(
      local_registry, name, (int)strlen(name), offset, 8, 0,
      address_type, target->tok ? target->tok : address->tok);

  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  node_t *temp_lhs = ps_node_new_lvar_expr_ref_for_in(
      arena_context, temp);
  *prefix = ps_node_new_assign_in(arena_context, temp_lhs, address);
  return ps_node_clone_lvalue_with_lhs_in(
      arena_context, target,
      ps_node_new_lvar_expr_ref_for_in(arena_context, temp));
}

node_t *lower_compound_assignment_expression(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry, node_t *node) {
  if (!node || node->kind != ND_ASSIGN ||
      !node->is_source_compound_assignment || !node->lhs || !node->rhs) {
    return node;
  }
  if (!lowering_context || !local_registry) return node;

  token_t *source_tok = node->tok;
  psx_syntax_node_kind_t binary_kind =
      compound_binary_kind(node->source_op);
  node_t *prefix = NULL;
  node_t *target = materialize_lvalue_address_once(
      lowering_context, local_registry, node->lhs, &prefix);
  node_t *rhs = node->rhs;
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  if ((binary_kind == ND_ADD || binary_kind == ND_SUB) &&
      ps_node_value_is_pointer_like(target)) {
    int scale = ps_lowering_type_deref_size(
        lowering_context, ps_node_get_type(target));
    if (scale > 1)
      rhs = ps_node_new_binary_for_target_in(
          arena_context, ps_lowering_target(lowering_context), ND_MUL, rhs,
          ps_node_new_num_in(arena_context, scale));
  }

  node_t *value = ps_node_new_binary_for_target_in(
      arena_context, ps_lowering_target(lowering_context),
      binary_kind, target, rhs);
  node_t *assign = ps_node_new_assign_in(
      ps_lowering_arena(lowering_context), target, value);
  node_t *lowered = prefix
                        ? ps_node_new_binary_for_target_in(
                              arena_context,
                              ps_lowering_target(lowering_context),
                              ND_COMMA, prefix, assign)
                        : assign;
  if (!lowered) return node;
  if (!lowered->tok) lowered->tok = source_tok;
  lowered->is_source_compound_assignment = 0;
  lowered->source_op = TK_EOF;
  return lowered;
}
