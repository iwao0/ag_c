#ifndef LOWERING_RUNTIME_CONTEXT_H
#define LOWERING_RUNTIME_CONTEXT_H

#include "frame_layout.h"
#include "static_local_lowering.h"
#include "../semantic/record_decl_table.h"
#include "../semantic/type_identity.h"
#include "../semantic/record_layout.h"
#include "../target_info.h"

typedef struct arena_context_t arena_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

typedef struct psx_lowering_context_t {
  arena_context_t *arena_context;
  ag_diagnostic_context_t *diagnostic_context;
  psx_resolution_store_t *resolution_store;
  const ag_target_info_t *target;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  frame_layout_t local_frame_layout;
  int static_local_sequences[PSX_STATIC_LOCAL_KIND_COUNT];
  int file_scope_compound_sequence;
  int local_compound_sequence;
  int aggregate_cast_temp_sequence;
  int compound_assignment_temp_sequence;
  int member_rvalue_sequence;
  int initializer_value_temp_sequence;
  int vla_typedef_bound_sequence;
} psx_lowering_context_t;

typedef struct {
  frame_layout_t local_frame_layout;
  int static_local_sequences[PSX_STATIC_LOCAL_KIND_COUNT];
  int file_scope_compound_sequence;
  int local_compound_sequence;
  int aggregate_cast_temp_sequence;
  int compound_assignment_temp_sequence;
  int member_rvalue_sequence;
  int initializer_value_temp_sequence;
  int vla_typedef_bound_sequence;
} psx_lowering_context_checkpoint_t;

/* target must outlive the lowering context. */
psx_lowering_context_t *ps_lowering_context_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context,
    const ag_target_info_t *target);
void ps_lowering_context_destroy(psx_lowering_context_t *ctx);
arena_context_t *ps_lowering_arena(
    const psx_lowering_context_t *ctx);
ag_diagnostic_context_t *ps_lowering_diagnostics(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_bind_resolution_store(
    psx_lowering_context_t *ctx, psx_resolution_store_t *store);
psx_resolution_store_t *ps_lowering_resolution_store(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_bind_semantic_types(
    psx_lowering_context_t *ctx,
    const psx_semantic_type_table_t *semantic_types);
const psx_semantic_type_table_t *ps_lowering_semantic_types(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_bind_record_decls(
    psx_lowering_context_t *ctx,
    const psx_record_decl_table_t *record_decls);
const psx_record_decl_table_t *ps_lowering_record_decls(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_bind_record_layouts(
    psx_lowering_context_t *ctx,
    const psx_record_layout_table_t *record_layouts);
const psx_record_layout_table_t *ps_lowering_record_layouts(
    const psx_lowering_context_t *ctx);
psx_type_id_t ps_lowering_type_id(
    const psx_lowering_context_t *ctx, const psx_type_t *type);
int ps_lowering_type_id_size(
    const psx_lowering_context_t *ctx, psx_type_id_t type_id);
int ps_lowering_type_id_alignment(
    const psx_lowering_context_t *ctx, psx_type_id_t type_id);
int ps_lowering_type_size(
    const psx_lowering_context_t *ctx, const psx_type_t *type);
int ps_lowering_type_deref_size(
    const psx_lowering_context_t *ctx, const psx_type_t *type);
int ps_lowering_type_alignment(
    const psx_lowering_context_t *ctx, const psx_type_t *type);
const ag_target_info_t *ps_lowering_target(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_reset_translation_unit(psx_lowering_context_t *ctx);
void psx_lowering_context_checkpoint(
    const psx_lowering_context_t *ctx,
    psx_lowering_context_checkpoint_t *checkpoint);
void psx_lowering_context_rollback(
    psx_lowering_context_t *ctx,
    const psx_lowering_context_checkpoint_t *checkpoint);

#endif
