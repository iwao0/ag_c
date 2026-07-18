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

int psx_plan_aggregate_source_cast_resolution(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    const psx_aggregate_cast_resolution_t *resolution,
    psx_aggregate_source_cast_plan_t *plan) {
  if (plan) *plan = (psx_aggregate_source_cast_plan_t){0};
  if (!lowering_context || !local_registry || !resolution || !plan ||
      resolution->status != PSX_AGGREGATE_CAST_STATUS_OK ||
      resolution->target_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  if (resolution->mode == PSX_AGGREGATE_CAST_COPY_VALUE) return 1;
  if (resolution->mode != PSX_AGGREGATE_CAST_INITIALIZE_MEMBER ||
      resolution->member_index < 0 ||
      resolution->member_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;

  const psx_type_t *target_type = psx_semantic_type_table_lookup(
      ps_lowering_semantic_types(lowering_context),
      resolution->target_qual_type.type_id);
  if (!ps_type_is_tag_aggregate(target_type)) return 0;
  const psx_record_layout_t *record_layout =
      psx_record_layout_table_lookup(
          ps_lowering_record_layouts(lowering_context),
          ps_type_record_id(target_type),
          ps_lowering_target(lowering_context));
  const psx_record_member_layout_t *member_layout =
      psx_record_layout_member(
          record_layout, resolution->member_index);
  if (!member_layout) return 0;

  int target_size = ps_lowering_type_size(
      lowering_context, target_type);
  int object_size = target_size > 0 ? target_size : 8;
  int object_align = ps_lowering_type_alignment(
      lowering_context, target_type);
  if (object_align <= 0) object_align = object_size;
  char *temporary_name =
      new_aggregate_temp_name(lowering_context);
  int offset = local_storage_allocate(
      lowering_context, object_size, object_align);
  lvar_t *temporary =
      temporary_name
          ? ps_local_registry_create_internal_storage_object_in(
                local_registry, temporary_name,
                (int)strlen(temporary_name), offset,
                object_size, object_align, target_type)
          : NULL;
  if (!temporary) return 0;

  plan->temporary = temporary;
  plan->member_qual_type = resolution->member_qual_type;
  plan->member_offset = member_layout->offset;
  plan->member_bit_width =
      0;
  plan->member_bit_offset =
      (unsigned char)(member_layout->bit_offset > 0
                          ? member_layout->bit_offset : 0);
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      ps_lowering_record_decls(lowering_context),
      ps_type_record_id(target_type));
  if (record && resolution->member_index < record->member_count) {
    const psx_record_member_decl_t *member =
        &record->members[resolution->member_index];
    plan->member_bit_width =
        (unsigned char)(member->bit_width > 0 ? member->bit_width : 0);
    plan->member_bit_is_signed = member->bit_is_signed ? 1 : 0;
  }
  return 1;
}

static void diagnose_aggregate_cast_resolution(
    psx_lowering_context_t *lowering_context, token_t *diag_tok,
    const psx_aggregate_cast_resolution_t *resolution) {
  if (!lowering_context || !resolution) return;
  ag_diagnostic_context_t *diagnostics =
      ps_lowering_diagnostics(lowering_context);
  token_kind_t tag_kind =
      (token_kind_t)resolution->target_tag_kind;
  switch (resolution->status) {
    case PSX_AGGREGATE_CAST_STATUS_TYPE_MISMATCH:
      ps_diag_ctx_in(
          diagnostics, diag_tok, "cast",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
          ps_ctx_tag_kind_spelling(tag_kind));
      return;
    case PSX_AGGREGATE_CAST_STATUS_STRUCT_EXTENSION_DISABLED:
      ps_diag_ctx_in(
          diagnostics, diag_tok, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
      return;
    case PSX_AGGREGATE_CAST_STATUS_UNION_EXTENSION_DISABLED:
      ps_diag_ctx_in(
          diagnostics, diag_tok, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED));
      return;
    case PSX_AGGREGATE_CAST_STATUS_UNSUPPORTED_TARGET:
      ps_diag_ctx_in(
          diagnostics, diag_tok, "cast",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
          ps_ctx_tag_kind_spelling(tag_kind));
      return;
    case PSX_AGGREGATE_CAST_STATUS_MEMBER_NOT_FOUND:
      ps_diag_ctx_in(
          diagnostics, diag_tok, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
      return;
    default:
      return;
  }
}

int psx_plan_aggregate_source_cast_qual_types(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type,
    token_t *diag_tok,
    const ag_compilation_options_t *options,
    psx_aggregate_source_cast_plan_t *plan) {
  if (plan) *plan = (psx_aggregate_source_cast_plan_t){0};
  if (!lowering_context || !local_registry || !options || !plan)
    return 0;
  psx_aggregate_cast_resolution_t resolution;
  psx_resolve_aggregate_cast_qual_types(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_decls(lowering_context),
      ps_lowering_record_layouts(lowering_context),
      ps_lowering_target(lowering_context), target_qual_type,
      operand_qual_type, options, &resolution);
  if (resolution.status != PSX_AGGREGATE_CAST_STATUS_OK) {
    diagnose_aggregate_cast_resolution(
        lowering_context, diag_tok, &resolution);
    return 0;
  }
  if (psx_plan_aggregate_source_cast_resolution(
          lowering_context, local_registry, &resolution, plan))
    return 1;
  ps_diag_ctx_in(
      ps_lowering_diagnostics(lowering_context), diag_tok, "cast", "%s",
      diag_message_for_in(
          ps_lowering_diagnostics(lowering_context),
          DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  return 0;
}

int psx_plan_aggregate_source_cast(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_source_cast_t *cast, token_t *fallback_diag_tok,
    const ag_compilation_options_t *options,
    psx_aggregate_source_cast_plan_t *plan) {
  node_t *node = cast ? &cast->base : NULL;
  if (!node || node->kind != ND_CAST || !node->is_source_cast ||
      !node->lhs)
    return 0;
  return psx_plan_aggregate_source_cast_qual_types(
      lowering_context, local_registry, ps_node_qual_type(node),
      ps_node_qual_type(node->lhs),
      node->tok ? node->tok : fallback_diag_tok, options, plan);
}
