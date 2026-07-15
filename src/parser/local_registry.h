#ifndef PARSER_LOCAL_REGISTRY_H
#define PARSER_LOCAL_REGISTRY_H

#include "lvar_public.h"

typedef struct psx_type_t psx_type_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct global_var_t global_var_t;

typedef struct {
  unsigned scope_seq;
  unsigned declaration_seq;
} psx_local_lookup_point_t;

psx_local_registry_t *ps_local_registry_create(void);
void ps_local_registry_destroy(psx_local_registry_t *registry);

unsigned ps_local_registry_current_scope_seq_in(
    const psx_local_registry_t *registry);
unsigned ps_local_registry_register_binding_event_in(
    psx_local_registry_t *registry);
int ps_local_registry_scope_is_visible_from_in(
    const psx_local_registry_t *registry,
    unsigned declaration_scope, unsigned reference_scope);
psx_local_lookup_point_t ps_local_registry_capture_lookup_point_in(
    const psx_local_registry_t *registry);
lvar_t *ps_local_registry_find_visible_in(
    const psx_local_registry_t *registry,
    char *name, int name_len, psx_local_lookup_point_t point);
void ps_local_registry_reset_in(psx_local_registry_t *registry);
void ps_local_registry_set_current_function_in(
    psx_local_registry_t *registry, char *name, int len);
void ps_local_registry_get_current_function_in(
    const psx_local_registry_t *registry,
    char **out_name, int *out_len);
void psx_local_registry_add_in(
    psx_local_registry_t *registry, lvar_t *var);
lvar_t *ps_local_registry_create_storage_object_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type);
lvar_t *ps_local_registry_create_type_binding_in(
    psx_local_registry_t *registry,
    char *name, int name_len, const psx_type_t *type);
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
    lvar_t *var, const psx_type_t *complete_type);
void ps_local_registry_set_vla_descriptor(
    lvar_t *var, int row_stride_frame_off, int strides_remaining,
    int row_stride_src_offset,
    int row_stride_elem_size);
void ps_local_registry_set_vla_param_inner_dims(
    lvar_t *var, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count);

#endif
