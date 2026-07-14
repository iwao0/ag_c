#ifndef PARSER_LOCAL_REGISTRY_H
#define PARSER_LOCAL_REGISTRY_H

#include "lvar_public.h"

typedef struct psx_type_t psx_type_t;

typedef struct {
  unsigned scope_seq;
  unsigned declaration_seq;
} psx_local_lookup_point_t;

unsigned ps_local_registry_current_scope_seq(void);
unsigned ps_local_registry_register_binding_event(void);
int ps_local_registry_scope_is_visible_from(
    unsigned declaration_scope, unsigned reference_scope);
psx_local_lookup_point_t ps_local_registry_capture_lookup_point(void);
lvar_t *ps_local_registry_find_visible(
    char *name, int name_len, psx_local_lookup_point_t point);
void ps_local_registry_reset(void);
void psx_local_registry_add(lvar_t *var);
lvar_t *ps_local_registry_create_storage_object(
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type);
lvar_t *ps_local_registry_create_type_binding(
    char *name, int name_len, const psx_type_t *type);
lvar_t *ps_local_registry_create_static_alias(
    char *name, int name_len, char *global_name, int global_name_len,
    const psx_type_t *type);
void ps_local_registry_update_storage_object(
    lvar_t *var, int offset, int storage_size, int alignment);
void ps_local_registry_mark_parameter(lvar_t *var, int is_byref);
void ps_local_registry_set_decl_type(
    lvar_t *var, const psx_type_t *decl_type);
void ps_local_registry_set_vla_descriptor(
    lvar_t *var, int row_stride_frame_off, int strides_remaining,
    int row_stride_src_offset,
    int row_stride_elem_size);
void ps_local_registry_set_vla_param_inner_dims(
    lvar_t *var, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count);

#endif
