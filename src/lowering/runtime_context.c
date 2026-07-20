#include "runtime_context.h"
#include "../target_info.h"
#include "../type_layout.h"

#include <stdlib.h>
#include <string.h>

psx_lowering_context_t *ps_lowering_context_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context,
    const ag_target_info_t *target) {
  if (!arena_context || !diagnostic_context ||
      !ag_target_info_is_valid(target))
    return NULL;
  psx_lowering_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->arena_context = arena_context;
    ctx->diagnostic_context = diagnostic_context;
    ctx->target = *target;
  }
  return ctx;
}

void ps_lowering_context_destroy(psx_lowering_context_t *ctx) {
  if (!ctx) return;
  free(ctx);
}

void ps_lowering_context_bind_semantic_types(
    psx_lowering_context_t *ctx,
    const psx_semantic_type_table_t *semantic_types) {
  if (ctx) ctx->semantic_types = semantic_types;
}

const psx_semantic_type_table_t *ps_lowering_semantic_types(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->semantic_types : NULL;
}

void ps_lowering_context_bind_record_decls(
    psx_lowering_context_t *ctx,
    const psx_record_decl_table_t *record_decls) {
  if (ctx) ctx->record_decls = record_decls;
}

const psx_record_decl_table_t *ps_lowering_record_decls(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->record_decls : NULL;
}

void ps_lowering_context_bind_record_layouts(
    psx_lowering_context_t *ctx,
    const psx_record_layout_table_t *record_layouts) {
  if (ctx) ctx->record_layouts = record_layouts;
}

const psx_record_layout_table_t *ps_lowering_record_layouts(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->record_layouts : NULL;
}

psx_type_id_t ps_lowering_type_id(
    const psx_lowering_context_t *ctx, const psx_type_t *type) {
  return psx_semantic_type_table_find(
             ps_lowering_semantic_types(ctx), type).type_id;
}

int ps_lowering_type_id_size(
    const psx_lowering_context_t *ctx, psx_type_id_t type_id) {
  return ps_type_sizeof_id(
      ps_lowering_semantic_types(ctx), ps_lowering_record_layouts(ctx),
      type_id, ps_lowering_target(ctx));
}

int ps_lowering_type_id_alignment(
    const psx_lowering_context_t *ctx, psx_type_id_t type_id) {
  return ps_type_alignof_id(
      ps_lowering_semantic_types(ctx), ps_lowering_record_layouts(ctx),
      type_id, ps_lowering_target(ctx));
}

int ps_lowering_type_size(
    const psx_lowering_context_t *ctx, const psx_type_t *type) {
  return ps_lowering_type_id_size(ctx, ps_lowering_type_id(ctx, type));
}

int ps_lowering_type_deref_size(
    const psx_lowering_context_t *ctx, const psx_type_t *type) {
  if (!type ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return 0;
  }
  return ps_lowering_type_size(ctx, type->base);
}

int ps_lowering_type_alignment(
    const psx_lowering_context_t *ctx, const psx_type_t *type) {
  return ps_lowering_type_id_alignment(
      ctx, ps_lowering_type_id(ctx, type));
}

const ag_target_info_t *ps_lowering_target(
    const psx_lowering_context_t *ctx) {
  return ctx ? &ctx->target : NULL;
}

arena_context_t *ps_lowering_arena(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->arena_context : NULL;
}

ag_diagnostic_context_t *ps_lowering_diagnostics(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->diagnostic_context : NULL;
}

void ps_lowering_context_bind_resolution_store(
    psx_lowering_context_t *ctx, psx_resolution_store_t *store) {
  if (ctx) ctx->resolution_store = store;
}

psx_resolution_store_t *ps_lowering_resolution_store(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->resolution_store : NULL;
}

void ps_lowering_context_reset_translation_unit(psx_lowering_context_t *ctx) {
  if (!ctx) return;
  frame_layout_reset(&ctx->local_frame_layout);
  memset(ctx->static_local_sequences, 0,
         sizeof(ctx->static_local_sequences));
  ctx->file_scope_compound_sequence = 0;
  ctx->local_compound_sequence = 0;
  ctx->aggregate_cast_temp_sequence = 0;
  ctx->compound_assignment_temp_sequence = 0;
  ctx->member_rvalue_sequence = 0;
  ctx->initializer_value_temp_sequence = 0;
  ctx->vla_typedef_bound_sequence = 0;
}

void psx_lowering_context_checkpoint(
    const psx_lowering_context_t *ctx,
    psx_lowering_context_checkpoint_t *checkpoint) {
  if (!ctx || !checkpoint) return;
  *checkpoint = (psx_lowering_context_checkpoint_t){
      .local_frame_layout = ctx->local_frame_layout,
      .file_scope_compound_sequence = ctx->file_scope_compound_sequence,
      .local_compound_sequence = ctx->local_compound_sequence,
      .aggregate_cast_temp_sequence = ctx->aggregate_cast_temp_sequence,
      .compound_assignment_temp_sequence =
          ctx->compound_assignment_temp_sequence,
      .member_rvalue_sequence = ctx->member_rvalue_sequence,
      .initializer_value_temp_sequence =
          ctx->initializer_value_temp_sequence,
      .vla_typedef_bound_sequence =
          ctx->vla_typedef_bound_sequence,
  };
  memcpy(checkpoint->static_local_sequences,
         ctx->static_local_sequences,
         sizeof(checkpoint->static_local_sequences));
}

void psx_lowering_context_rollback(
    psx_lowering_context_t *ctx,
    const psx_lowering_context_checkpoint_t *checkpoint) {
  if (!ctx || !checkpoint) return;
  ctx->local_frame_layout = checkpoint->local_frame_layout;
  memcpy(ctx->static_local_sequences,
         checkpoint->static_local_sequences,
         sizeof(ctx->static_local_sequences));
  ctx->file_scope_compound_sequence =
      checkpoint->file_scope_compound_sequence;
  ctx->local_compound_sequence = checkpoint->local_compound_sequence;
  ctx->aggregate_cast_temp_sequence =
      checkpoint->aggregate_cast_temp_sequence;
  ctx->compound_assignment_temp_sequence =
      checkpoint->compound_assignment_temp_sequence;
  ctx->member_rvalue_sequence = checkpoint->member_rvalue_sequence;
  ctx->initializer_value_temp_sequence =
      checkpoint->initializer_value_temp_sequence;
  ctx->vla_typedef_bound_sequence =
      checkpoint->vla_typedef_bound_sequence;
}
