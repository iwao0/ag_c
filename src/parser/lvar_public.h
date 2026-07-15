#ifndef PARSER_LVAR_PUBLIC_H
#define PARSER_LVAR_PUBLIC_H

#include "core.h"
#include "../semantic/type_identity.h"

typedef struct lvar_t lvar_t;
typedef struct psx_type_t psx_type_t;
typedef struct psx_lvar_usage_region_t psx_lvar_usage_region_t;

typedef struct {
  const char *name;
  int name_len;
  unsigned scope_seq;
  int is_used;
  int is_unevaluated_used;
  int is_address_taken;
  int is_initialized;
  int suppress_unreachable_warnings;
  int is_param;
  int is_array;
  int is_static_local;
  psx_lvar_usage_region_t *decl_region;
} psx_lvar_registry_view_t;

lvar_t *ps_lvar_next_all(const lvar_t *var);
lvar_t *ps_lvar_find_owner(lvar_t *head, int offset);
psx_lvar_registry_view_t ps_lvar_registry_view(const lvar_t *var);
const psx_type_t *ps_lvar_get_decl_type(const lvar_t *var);
psx_type_id_t ps_lvar_decl_type_id(const lvar_t *var);

const char *ps_lvar_name(const lvar_t *var);
int ps_lvar_name_len(const lvar_t *var);
int ps_lvar_offset(const lvar_t *var);
int ps_lvar_decl_sizeof(const lvar_t *var, int fallback_size);
int ps_lvar_storage_size(const lvar_t *var, int fallback_size);
int ps_lvar_elem_size(const lvar_t *var, int fallback_size);
int ps_lvar_align_bytes(const lvar_t *var);
int ps_lvar_is_param(const lvar_t *var);
int ps_lvar_is_static_local(const lvar_t *var);
int ps_lvar_is_vla(const lvar_t *var);
int ps_lvar_is_array(const lvar_t *var);
int ps_lvar_array_scalar_element_size(const lvar_t *var);
int ps_lvar_array_flat_element_count(const lvar_t *var);
int ps_lvar_array_designator_stride_elements(const lvar_t *var, int depth);
int ps_lvar_is_complex(const lvar_t *var);
int ps_lvar_is_tag_pointer(const lvar_t *var);
int ps_lvar_value_is_pointer_like(const lvar_t *var);
int ps_lvar_is_tag_aggregate(const lvar_t *var);
int ps_lvar_is_struct_aggregate(const lvar_t *var);
int ps_lvar_is_union_aggregate(const lvar_t *var);
token_kind_t ps_lvar_tag_kind(const lvar_t *var);
tk_float_kind_t ps_lvar_fp_kind(const lvar_t *var);

int ps_lvar_vla_row_stride_frame_off(const lvar_t *var);
int ps_lvar_vla_strides_remaining(const lvar_t *var);
int ps_lvar_vla_row_stride_elem_size(const lvar_t *var);
int ps_lvar_vla_row_stride_src_offset(const lvar_t *var);
int ps_lvar_vla_param_inner_dim_count(const lvar_t *var);
int ps_lvar_vla_param_inner_dim_const(const lvar_t *var, int idx);
int ps_lvar_vla_param_inner_dim_src_offset(const lvar_t *var, int idx);

#endif
