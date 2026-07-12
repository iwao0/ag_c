#ifndef PARSER_LOCAL_REGISTRY_H
#define PARSER_LOCAL_REGISTRY_H

#include "lvar_public.h"

typedef struct psx_type_t psx_type_t;

unsigned ps_local_registry_current_scope_seq(void);
void ps_local_registry_reset(void);
void ps_local_registry_add(lvar_t *var);
lvar_t *ps_local_registry_create_storage_object(
    char *name, int name_len, int offset, int storage_size,
    int element_size, int is_array, int alignment);
lvar_t *ps_local_registry_create_type_binding(
    char *name, int name_len, const psx_type_t *type);
lvar_t *ps_local_registry_create_static_alias(
    char *name, int name_len, int storage_size, int element_size,
    char *global_name, int global_name_len);
void ps_local_registry_update_storage_object(
    lvar_t *var, int offset, int storage_size,
    int element_size, int is_array, int alignment);
void ps_local_registry_mark_parameter(lvar_t *var, int is_byref);
void ps_local_registry_set_decl_type(
    lvar_t *var, const psx_type_t *decl_type);
void ps_local_registry_set_vla_descriptor(
    lvar_t *var, int outer_stride, int row_stride_frame_off,
    int strides_remaining, int row_stride_src_offset,
    int row_stride_elem_size);
void ps_local_registry_set_vla_param_inner_dims(
    lvar_t *var, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count);

#endif
