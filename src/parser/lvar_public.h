#ifndef PARSER_LVAR_PUBLIC_H
#define PARSER_LVAR_PUBLIC_H

#include "core.h"

typedef struct lvar_t lvar_t;

lvar_t *psx_lvar_next_all(const lvar_t *var);
lvar_t *psx_lvar_find_owner(lvar_t *head, int offset);

int psx_lvar_offset(const lvar_t *var);
int psx_lvar_storage_size(const lvar_t *var, int fallback_size);
int psx_lvar_elem_size(const lvar_t *var, int fallback_size);
int psx_lvar_align_bytes(const lvar_t *var);
int psx_lvar_is_param(const lvar_t *var);
int psx_lvar_is_static_local(const lvar_t *var);
int psx_lvar_is_vla(const lvar_t *var);
int psx_lvar_is_array(const lvar_t *var);
int psx_lvar_is_complex(const lvar_t *var);
int psx_lvar_is_tag_pointer(const lvar_t *var);
int psx_lvar_value_is_pointer_like(const lvar_t *var);
int psx_lvar_is_tag_aggregate(const lvar_t *var);
int psx_lvar_is_struct_aggregate(const lvar_t *var);
int psx_lvar_is_union_aggregate(const lvar_t *var);
int psx_lvar_pointer_qual_levels(const lvar_t *var);
token_kind_t psx_lvar_tag_kind(const lvar_t *var);
tk_float_kind_t psx_lvar_fp_kind(const lvar_t *var);
psx_decl_funcptr_sig_t psx_lvar_funcptr_sig(const lvar_t *src);

int psx_lvar_vla_row_stride_frame_off(const lvar_t *var);
int psx_lvar_vla_row_stride_elem_size(const lvar_t *var);
int psx_lvar_vla_row_stride_src_offset(const lvar_t *var);
int psx_lvar_vla_param_inner_dim_count(const lvar_t *var);
int psx_lvar_vla_param_inner_dim_const(const lvar_t *var, int idx);
int psx_lvar_vla_param_inner_dim_src_offset(const lvar_t *var, int idx);

#endif
