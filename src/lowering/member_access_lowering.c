#include "member_access_lowering.h"

#include "../declaration_pipeline.h"
#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int member_rvalue_sequence;

static char *new_member_rvalue_name(void) {
  int sequence = member_rvalue_sequence++;
  int len = snprintf(NULL, 0, "__member_rvalue_%d", sequence);
  char *name = calloc((size_t)len + 1, 1);
  if (!name) return NULL;
  snprintf(name, (size_t)len + 1, "__member_rvalue_%d", sequence);
  return name;
}

static struct lvar_t *create_aggregate_temporary(
    node_member_access_t *access,
    const token_t *fallback_diag_tok) {
  token_t *tok = access->base.tok
                     ? access->base.tok
                     : (token_t *)fallback_diag_tok;
  const psx_type_t *object_type = ps_type_find_aggregate_object_type(
      ps_node_get_type(access->base.lhs));
  if (!object_type || ps_type_sizeof(object_type) <= 0) {
    ps_diag_ctx(tok, "member", "aggregate rvalue size resolution failed");
  }
  char *name = new_member_rvalue_name();
  if (!name) ps_diag_ctx(tok, "member", "temporary name allocation failed");
  psx_type_t *type = ps_type_clone(object_type);
  struct lvar_t *temporary = psx_apply_temporary_local_declaration_pipeline(
      &(psx_temporary_local_declaration_pipeline_request_t){
          .name = name,
          .name_len = (int)strlen(name),
          .type = type,
      });
  if (!temporary)
    ps_diag_ctx(tok, "member", "failed to create aggregate temporary");
  return temporary;
}

static node_t *materialize_call_rvalue(
    node_member_access_t *access, node_t *base,
    const token_t *fallback_diag_tok) {
  struct lvar_t *temporary = create_aggregate_temporary(
      access, fallback_diag_tok);
  node_t *target = ps_node_new_lvar_expr_ref_for(temporary);
  node_t *assign = ps_node_new_assign(target, base);
  return ps_node_new_binary(
      ND_COMMA, assign,
      ps_node_new_lvar_expr_ref_for(temporary));
}

static node_t *materialize_ternary_rvalue(
    node_member_access_t *access, node_t *base,
    const token_t *fallback_diag_tok) {
  node_ctrl_t *ternary = (node_ctrl_t *)base;
  struct lvar_t *temporary = create_aggregate_temporary(
      access, fallback_diag_tok);
  node_ctrl_t *select = arena_alloc(sizeof(*select));
  select->base.kind = ND_TERNARY;
  select->base.lhs = ternary->base.lhs;
  select->base.rhs = ps_node_new_assign(
      ps_node_new_lvar_expr_ref_for(temporary),
      ternary->base.rhs);
  select->els = ps_node_new_assign(
      ps_node_new_lvar_expr_ref_for(temporary),
      ternary->els);
  return ps_node_new_binary(
      ND_COMMA, (node_t *)select,
      ps_node_new_lvar_expr_ref_for(temporary));
}

node_t *lower_member_access_expression(
    node_member_access_t *access,
    const token_t *fallback_diag_tok) {
  if (!access || !access->base.lhs || !access->resolved_member)
    return (node_t *)access;
  node_t *base = access->base.lhs;
  if (!access->from_pointer) {
    if (base->kind == ND_FUNCALL) {
      base = materialize_call_rvalue(
          access, base, fallback_diag_tok);
    } else if (base->kind == ND_TERNARY &&
               ps_type_find_aggregate_object_type(
                   ps_node_get_type(base))) {
      base = materialize_ternary_rvalue(
          access, base, fallback_diag_tok);
    }
  }

  node_t *address = base;
  if (!access->from_pointer) {
    if (base->kind == ND_COMMA && base->rhs) {
      address = ps_node_new_binary(
          ND_COMMA, base->lhs,
          ps_node_new_addr_value_for(base->rhs));
    } else {
      address = ps_node_new_addr_value_for(base);
    }
  }
  node_t *result = ps_node_new_tag_member_deref_for(
      address, base, access->resolved_member);
  if (result) result->tok = access->base.tok;
  return result ? result : (node_t *)access;
}
