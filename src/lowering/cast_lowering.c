#include "cast_lowering.h"
#include "local_storage.h"
#include "runtime_context.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/local_registry.h"
#include "../semantic/resolved_node_type.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const psx_type_t *target;
  const psx_type_t *value;
  token_kind_t tag_kind;
  int elem_size;
} aggregate_cast_target_t;

static const psx_type_t *target_value_type(const psx_type_t *type) {
  const psx_type_t *value = type;
  while (value &&
         (value->kind == PSX_TYPE_POINTER || value->kind == PSX_TYPE_ARRAY))
    value = value->base;
  if (value && value->kind == PSX_TYPE_FUNCTION) value = value->base;
  return value;
}

static aggregate_cast_target_t aggregate_target(
    const psx_lowering_context_t *lowering_context,
    const psx_type_t *target) {
  const psx_type_t *value = target_value_type(target);
  return (aggregate_cast_target_t){
      .target = target,
      .value = value,
      .tag_kind = ps_type_tag_token_kind(value),
      .elem_size = ps_lowering_type_size(lowering_context, value),
  };
}

static int same_tag_value(
    node_t *expr, const aggregate_cast_target_t *target) {
  if (!expr || !target || !target->value) return 0;
  node_t *value = expr;
  while (value && value->kind == ND_COMMA) value = value->rhs;
  if (!value) return 0;
  if (value->kind == ND_TERNARY) {
    node_ctrl_t *ternary = (node_ctrl_t *)value;
    return same_tag_value(ternary->base.rhs, target) &&
           same_tag_value(ternary->els, target);
  }
  const psx_type_t *value_type = ps_node_get_type(value);
  return ps_type_is_tag_aggregate(value_type) &&
         ps_type_tag_identity_matches(value_type, target->value);
}

static int size_compatible_tag_value(
    const psx_lowering_context_t *lowering_context, node_t *expr,
    const aggregate_cast_target_t *target) {
  if (!expr || !target) return 0;
  node_t *value = expr;
  while (value && value->kind == ND_COMMA) value = value->rhs;
  if (!value) return 0;
  if (value->kind == ND_TERNARY) {
    node_ctrl_t *ternary = (node_ctrl_t *)value;
    return size_compatible_tag_value(
               lowering_context, ternary->base.rhs, target) &&
           size_compatible_tag_value(
               lowering_context, ternary->els, target);
  }
  const psx_type_t *value_type = ps_node_get_type(value);
  int value_size = ps_lowering_type_size(lowering_context, value_type);
  return ps_type_is_tag_aggregate(value_type) &&
         ps_type_tag_token_kind(value_type) == target->tag_kind &&
         value_size > 0 && target->elem_size > 0 &&
         value_size == target->elem_size;
}

static char *new_aggregate_temp_name(
    psx_lowering_context_t *lowering_context) {
  int sequence = lowering_context->aggregate_cast_temp_sequence++;
  int length = snprintf(
      NULL, 0, "__aggregate_cast_%d", sequence);
  char *name = calloc((size_t)length + 1, 1);
  if (!name) return NULL;
  snprintf(
      name, (size_t)length + 1,
      "__aggregate_cast_%d", sequence);
  return name;
}

int psx_plan_aggregate_source_cast(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_source_cast_t *cast, token_t *fallback_diag_tok,
    const ag_compilation_options_t *options,
    psx_aggregate_source_cast_plan_t *plan) {
  if (plan) *plan = (psx_aggregate_source_cast_plan_t){0};
  node_t *node = cast ? &cast->base : NULL;
  if (!lowering_context || !local_registry || !node ||
      node->kind != ND_CAST || !node->is_source_cast || !options ||
      !plan)
    return 0;
  const psx_type_t *target_type = ps_node_get_type(node);
  if (!ps_type_is_tag_aggregate(target_type)) return 0;
  aggregate_cast_target_t target =
      aggregate_target(lowering_context, target_type);
  node_t *operand = node->lhs;
  if (same_tag_value(operand, &target) ||
      (options->enable_size_compatible_nonscalar_cast &&
       size_compatible_tag_value(
           lowering_context, operand, &target))) {
    return 1;
  }

  token_t *diag_tok = node->tok ? node->tok : fallback_diag_tok;
  ag_diagnostic_context_t *diagnostics =
      ps_lowering_diagnostics(lowering_context);
  const psx_type_t *operand_type = ps_node_get_type(operand);
  if (ps_type_is_tag_aggregate(operand_type)) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "cast",
        diag_message_for_in(
            diagnostics,
            DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
        ps_ctx_tag_kind_spelling(target.tag_kind));
  }
  if (target.tag_kind == TK_STRUCT &&
      !options->enable_struct_scalar_pointer_cast) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "cast", "%s",
        diag_message_for_in(
            diagnostics,
            DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
  }
  if (target.tag_kind == TK_UNION &&
      !options->enable_union_scalar_pointer_cast) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "cast", "%s",
        diag_message_for_in(
            diagnostics,
            DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED));
  }
  if (target.tag_kind != TK_STRUCT && target.tag_kind != TK_UNION) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "cast",
        diag_message_for_in(
            diagnostics,
            DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
        ps_ctx_tag_kind_spelling(target.tag_kind));
  }

  const psx_record_member_decl_t *member = NULL;
  int member_index = -1;
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      ps_lowering_record_decls(lowering_context),
      ps_type_record_id(target.value));
  if (record) {
    for (int i = 0; i < record->member_count; i++) {
      if (record->members[i].len <= 0) continue;
      member = &record->members[i];
      member_index = i;
      break;
    }
  }
  if (!member) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "cast", "%s",
        diag_message_for_in(
            diagnostics,
            DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  const psx_record_layout_t *record_layout =
      psx_record_layout_table_lookup(
          ps_lowering_record_layouts(lowering_context),
          ps_type_record_id(target.value),
          ps_lowering_target(lowering_context));
  const psx_record_member_layout_t *member_layout =
      psx_record_layout_member(record_layout, member_index);
  if (!member_layout) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "cast", "%s",
        diag_message_for_in(
            diagnostics,
            DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  int object_size = target.elem_size > 0 ? target.elem_size : 8;
  char *temporary_name =
      new_aggregate_temp_name(lowering_context);
  int offset = local_storage_allocate(
      lowering_context, object_size, object_size);
  lvar_t *temporary =
      temporary_name
          ? ps_local_registry_create_storage_object_in(
                local_registry, temporary_name,
                (int)strlen(temporary_name), offset,
                object_size, object_size, target.target, diag_tok)
          : NULL;
  if (!temporary) return 0;

  plan->temporary = temporary;
  plan->member_qual_type = member->decl_qual_type;
  plan->member_offset = member_layout->offset;
  plan->member_bit_width =
      (unsigned char)(member->bit_width > 0 ? member->bit_width : 0);
  plan->member_bit_offset =
      (unsigned char)(member_layout->bit_offset > 0
                          ? member_layout->bit_offset : 0);
  plan->member_bit_is_signed =
      member->bit_is_signed ? 1 : 0;
  return 1;
}
