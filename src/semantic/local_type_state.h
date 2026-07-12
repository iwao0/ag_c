#ifndef SEMANTIC_LOCAL_TYPE_STATE_H
#define SEMANTIC_LOCAL_TYPE_STATE_H

#include "../parser/core.h"

typedef struct lvar_t lvar_t;
typedef struct psx_type_t psx_type_t;

void psx_decl_set_lvar_decl_type(
    lvar_t *var, const psx_type_t *decl_type);
void psx_decl_set_lvar_vla_descriptor(
    lvar_t *var, int outer_stride, int row_stride_frame_off,
    int strides_remaining, int row_stride_src_offset,
    int row_stride_elem_size);
void psx_decl_set_lvar_vla_param_inner_dims(
    lvar_t *var, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count);
void psx_decl_set_lvar_type_sig(lvar_t *var, char *type_sig);

#endif
