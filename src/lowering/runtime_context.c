#include "runtime_context.h"
#include "../target_info.h"
#include "../type_layout.h"

#include <stdlib.h>
#include <string.h>

psx_lowering_context_t *ps_lowering_context_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context) {
  if (!arena_context || !diagnostic_context) return NULL;
  psx_lowering_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->arena_context = arena_context;
    ctx->diagnostic_context = diagnostic_context;
    ctx->target = ag_target_info_host();
  }
  return ctx;
}

void ps_lowering_context_destroy(psx_lowering_context_t *ctx) {
  if (!ctx) return;
  free(ctx);
}

void ps_lowering_context_bind_target(
    psx_lowering_context_t *ctx, const ag_target_info_t *target) {
  if (!ctx) return;
  ctx->target = target ? *target : ag_target_info_host();
  ctx->target.pointer_size = ag_target_info_pointer_size(&ctx->target);
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
  return ps_type_sizeof_id_with_records(
      ps_lowering_semantic_types(ctx), ps_lowering_record_layouts(ctx),
      type_id, ps_lowering_target(ctx));
}

int ps_lowering_type_id_alignment(
    const psx_lowering_context_t *ctx, psx_type_id_t type_id) {
  return ps_type_alignof_id_with_records(
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
}
