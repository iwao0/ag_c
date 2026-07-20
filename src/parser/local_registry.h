#ifndef PARSER_LOCAL_REGISTRY_H
#define PARSER_LOCAL_REGISTRY_H

#include "lvar_public.h"
#include "../semantic/scope_graph.h"

typedef struct psx_type_t psx_type_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct global_var_t global_var_t;
typedef struct token_t token_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_lvar_usage_region_t psx_lvar_usage_region_t;

typedef struct {
  void *state;
} psx_local_registry_checkpoint_t;

typedef struct psx_local_lookup_point_t {
  unsigned scope_seq;
  unsigned declaration_seq;
} psx_local_lookup_point_t;

psx_local_registry_t *ps_local_registry_create(
    ag_diagnostic_context_t *diagnostic_context);
void ps_local_registry_destroy(psx_local_registry_t *registry);
ag_diagnostic_context_t *ps_local_registry_diagnostics(
    const psx_local_registry_t *registry);
void ps_local_registry_bind_semantic_types(
    psx_local_registry_t *registry,
    const psx_semantic_type_table_t *semantic_types);
void ps_local_registry_bind_scope_graph(
    psx_local_registry_t *registry, psx_scope_graph_t *scope_graph);
psx_scope_graph_t *ps_local_registry_scope_graph(
    const psx_local_registry_t *registry);
int psx_local_registry_checkpoint_begin(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint);
int psx_local_registry_checkpoint_is_active(
    const psx_local_registry_t *registry);
void psx_local_registry_checkpoint_commit(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint);
void psx_local_registry_checkpoint_rollback(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint);

unsigned ps_local_registry_current_scope_seq_in(
    const psx_local_registry_t *registry);
unsigned ps_local_registry_next_scope_seq_in(
    const psx_local_registry_t *registry);
psx_local_lookup_point_t ps_local_registry_capture_lookup_point_in(
    const psx_local_registry_t *registry);
lvar_t *ps_local_registry_find_visible_in(
    const psx_local_registry_t *registry,
    char *name, int name_len, psx_local_lookup_point_t point);
void ps_local_registry_reset_in(psx_local_registry_t *registry);
void ps_local_registry_reset_translation_unit_in(
    psx_local_registry_t *registry);
void ps_local_registry_prepare_function_resolution_in(
    psx_local_registry_t *registry);
void ps_local_registry_enter_prototype_scope_in(
    psx_local_registry_t *registry);
void ps_local_registry_enter_translation_unit_in(
    psx_local_registry_t *registry);
void ps_local_registry_set_current_function_in(
    psx_local_registry_t *registry, char *name, int len);
void ps_local_registry_get_current_function_in(
    const psx_local_registry_t *registry,
    char **out_name, int *out_len);
psx_lvar_usage_region_t *
ps_local_registry_set_current_usage_region_in(
    psx_local_registry_t *registry,
    psx_lvar_usage_region_t *region);
void psx_local_registry_add_in(
    psx_local_registry_t *registry, lvar_t *var);
lvar_t *ps_local_registry_create_storage_object_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type,
    token_t *diagnostic_token);
lvar_t *ps_local_registry_create_internal_storage_object_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type);
lvar_t *ps_local_registry_create_type_binding_in(
    psx_local_registry_t *registry,
    char *name, int name_len, const psx_type_t *type,
    token_t *diagnostic_token);
lvar_t *ps_local_registry_create_static_alias_in(
    psx_local_registry_t *registry,
    global_var_t *global,
    char *name, int name_len, char *global_name, int global_name_len,
    const psx_type_t *type);
void ps_local_registry_update_storage_object_in(
    psx_local_registry_t *registry,
    lvar_t *var, int offset, int storage_size, int alignment);

void ps_local_registry_mark_parameter(lvar_t *var, int is_byref);
int ps_local_registry_complete_array_type(
    psx_local_registry_t *registry, lvar_t *var,
    const psx_type_t *complete_type);
void ps_local_registry_set_vla_descriptor(
    lvar_t *var, int row_stride_frame_off, int strides_remaining,
    int row_stride_src_offset,
    int row_stride_elem_size);
void ps_local_registry_set_vla_param_inner_dims(
    psx_local_registry_t *registry, lvar_t *var,
    const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count,
    token_t *diagnostic_token);

#endif
