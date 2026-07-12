#ifndef PARSER_LVAR_INTERNAL_H
#define PARSER_LVAR_INTERNAL_H

#include "lvar_public.h"

struct lvar_t {
  lvar_t *next;
  lvar_t *next_all;
  lvar_t *next_binding;
  lvar_t *next_hash;
  lvar_t *next_offhash;
  unsigned scope_seq;
  unsigned declaration_seq;
  char *name;
  int len;
  int offset;
  int size;
  int elem_size;
  unsigned int is_array : 1;
  unsigned int is_vla : 1;
  unsigned int is_byref_param : 1;
  unsigned int is_used : 1;
  unsigned int is_unevaluated_used : 1;
  unsigned int is_address_taken : 1;
  unsigned int suppress_unreachable_warnings : 1;
  unsigned int is_param : 1;
  unsigned int is_initialized : 1;
  unsigned int is_static_local : 1;
  char *static_global_name;
  int static_global_name_len;
  int align_bytes;
  int used_count;
  int outer_stride;
  int mid_stride;
  int *extra_strides;
  unsigned char extra_strides_count;
  int vla_row_stride_frame_off;
  int vla_strides_remaining;
  psx_type_t *decl_type;
  int vla_row_stride_src_offset;
  short vla_row_stride_elem_size;
  short vla_param_inner_dim_consts[7];
  int vla_param_inner_dim_src_offsets[7];
  unsigned char vla_param_inner_dim_count;
  psx_lvar_usage_region_t *decl_region;
};

#endif
