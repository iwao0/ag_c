#include "member_access_lowering.h"
#include "runtime_context.h"

#include "../declaration_pipeline.h"
#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/type_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *new_member_rvalue_name(
    psx_lowering_context_t *lowering_context) {
  int sequence = lowering_context->member_rvalue_sequence++;
  int len = snprintf(NULL, 0, "__member_rvalue_%d", sequence);
  char *name = calloc((size_t)len + 1, 1);
  if (!name) return NULL;
  snprintf(name, (size_t)len + 1, "__member_rvalue_%d", sequence);
  return name;
}

static const psx_record_member_layout_t *resolve_target_member_layout(
    const psx_lowering_context_t *lowering_context,
    const node_member_access_t *access) {
  if (!lowering_context || !access ||
      access->resolved_record_id == PSX_RECORD_ID_INVALID ||
      access->resolved_member_index < 0)
    return NULL;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      ps_lowering_record_layouts(lowering_context),
      access->resolved_record_id, ps_lowering_target(lowering_context));
  return psx_record_layout_member(layout, access->resolved_member_index);
}

static struct lvar_t *create_aggregate_temporary(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_member_access_t *access,
    const token_t *fallback_diag_tok) {
  token_t *tok = access->base.tok
                     ? access->base.tok
                     : (token_t *)fallback_diag_tok;
  const psx_type_t *object_type = ps_type_find_aggregate_object_type(
      ps_node_get_type(access->base.lhs));
  if (!object_type ||
      ps_lowering_type_size(lowering_context, object_type) <= 0) {
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), tok, "member",
        "aggregate rvalue size resolution failed");
  }
  char *name = new_member_rvalue_name(lowering_context);
  if (!name)
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), tok, "member",
        "temporary name allocation failed");
  psx_type_t *type = ps_type_clone_in(
      ps_lowering_arena(lowering_context), object_type);
  struct lvar_t *temporary = psx_apply_temporary_local_declaration_pipeline(
      &(psx_temporary_local_declaration_pipeline_request_t){
          .local_registry = local_registry,
          .lowering_context = lowering_context,
          .name = name,
          .name_len = (int)strlen(name),
          .type = type,
      });
  if (!temporary)
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), tok, "member",
        "failed to create aggregate temporary");
  return temporary;
}

static node_t *materialize_call_rvalue(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_member_access_t *access, node_t *base,
    const token_t *fallback_diag_tok) {
  struct lvar_t *temporary = create_aggregate_temporary(
      lowering_context, local_registry, access, fallback_diag_tok);
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  node_t *target = ps_node_new_lvar_expr_ref_for_in(
      arena_context, temporary);
  node_t *assign = ps_node_new_assign_in(
      arena_context, target, base);
  return ps_node_new_binary_for_target_in(
      arena_context, ps_lowering_target(lowering_context), ND_COMMA, assign,
      ps_node_new_lvar_expr_ref_for_in(arena_context, temporary));
}

static node_t *materialize_ternary_rvalue(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_member_access_t *access, node_t *base,
    const token_t *fallback_diag_tok) {
  node_ctrl_t *ternary = (node_ctrl_t *)base;
  struct lvar_t *temporary = create_aggregate_temporary(
      lowering_context, local_registry, access, fallback_diag_tok);
  node_ctrl_t *select = arena_alloc_in(
      ps_lowering_arena(lowering_context), sizeof(*select));
  select->base.kind = ND_TERNARY;
  select->base.lhs = ternary->base.lhs;
  select->base.rhs = ps_node_new_assign_in(
      ps_lowering_arena(lowering_context),
      ps_node_new_lvar_expr_ref_for_in(
          ps_lowering_arena(lowering_context), temporary),
      ternary->base.rhs);
  select->els = ps_node_new_assign_in(
      ps_lowering_arena(lowering_context),
      ps_node_new_lvar_expr_ref_for_in(
          ps_lowering_arena(lowering_context), temporary),
      ternary->els);
  return ps_node_new_binary_for_target_in(
      ps_lowering_arena(lowering_context),
      ps_lowering_target(lowering_context), ND_COMMA, (node_t *)select,
      ps_node_new_lvar_expr_ref_for_in(
          ps_lowering_arena(lowering_context), temporary));
}

node_t *lower_member_access_expression_in(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_member_access_t *access,
    const token_t *fallback_diag_tok) {
  if (!lowering_context || !local_registry || !access ||
      !access->base.lhs || !access->resolved_member)
    return (node_t *)access;
  const psx_record_member_decl_t *member = access->resolved_member;
  const psx_record_member_layout_t *member_layout =
      resolve_target_member_layout(lowering_context, access);
  if (!member_layout) {
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), access->base.tok,
        "member", "resolved member layout is unavailable");
    return (node_t *)access;
  }
  node_t *base = access->base.lhs;
  if (!access->from_pointer) {
    if (base->kind == ND_FUNCALL) {
      base = materialize_call_rvalue(
          lowering_context, local_registry, access, base,
          fallback_diag_tok);
    } else if (base->kind == ND_TERNARY &&
               ps_type_find_aggregate_object_type(
                   ps_node_get_type(base))) {
      base = materialize_ternary_rvalue(
          lowering_context, local_registry, access, base,
          fallback_diag_tok);
    }
  }

  node_t *address = base;
  if (!access->from_pointer) {
    if (base->kind == ND_COMMA && base->rhs) {
      address = ps_node_new_binary_for_target_in(
          ps_lowering_arena(lowering_context),
          ps_lowering_target(lowering_context), ND_COMMA, base->lhs,
          ps_node_new_addr_value_for_in(
              ps_lowering_arena(lowering_context), base->rhs));
    } else {
      address = ps_node_new_addr_value_for_in(
          ps_lowering_arena(lowering_context), base);
    }
  }
  node_t *result = ps_node_new_tag_member_deref_with_layout_for_in(
      ps_lowering_arena(lowering_context),
      ps_lowering_target(lowering_context), address, base,
      member_layout->offset, psx_record_member_decl_type(member),
      member->bit_is_signed, member->bit_width,
      member_layout->bit_offset);
  if (result) result->tok = access->base.tok;
  return result ? result : (node_t *)access;
}
