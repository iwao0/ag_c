#include "assignment_lowering.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compound_assignment_temp_seq;

static node_kind_t compound_binary_kind(token_kind_t op) {
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

static char *new_compound_assignment_temp_name(void) {
  int seq = compound_assignment_temp_seq++;
  int len = snprintf(NULL, 0, "__compound_assign_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__compound_assign_%d", seq);
  return name;
}

static node_t *materialize_lvalue_address_once(node_t *target,
                                               node_t **prefix) {
  if (!target || target->kind != ND_DEREF || !target->lhs) return target;
  node_t *address = target->lhs;
  const psx_type_t *address_type = ps_node_get_type(address);
  if (!address_type) return target;
  char *name = new_compound_assignment_temp_name();
  lvar_t *temp = ps_decl_register_lvar_typed_align(
      name, (int)strlen(name), 8, 0, address_type);

  node_t *temp_lhs = ps_node_new_lvar_expr_ref_for(temp);
  *prefix = ps_node_new_assign(temp_lhs, address);
  return ps_node_clone_lvalue_with_lhs(
      target, ps_node_new_lvar_expr_ref_for(temp));
}

void lower_compound_assignment_expression(node_t *node) {
  if (!node || node->kind != ND_ASSIGN ||
      !node->is_source_compound_assignment || !node->lhs || !node->rhs) {
    return;
  }

  token_t *source_tok = node->tok;
  node_kind_t binary_kind = compound_binary_kind(node->source_op);
  node_t *prefix = NULL;
  node_t *target = materialize_lvalue_address_once(node->lhs, &prefix);
  node_t *rhs = node->rhs;
  if ((binary_kind == ND_ADD || binary_kind == ND_SUB) &&
      ps_node_value_is_pointer_like(target)) {
    int scale = ps_node_deref_size(target);
    if (scale > 1)
      rhs = ps_node_new_binary(ND_MUL, rhs, ps_node_new_num(scale));
  }

  node_t *value = ps_node_new_binary(binary_kind, target, rhs);
  node_t *assign = ps_node_new_assign(target, value);
  node_t *lowered = prefix
                        ? ps_node_new_binary(ND_COMMA, prefix, assign)
                        : assign;
  *node = *lowered;
  node->tok = source_tok;
  node->is_source_compound_assignment = 0;
  node->source_op = TK_EOF;
}
