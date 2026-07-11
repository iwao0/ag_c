#ifndef PARSER_TYPE_H
#define PARSER_TYPE_H

#include "core.h"

typedef enum {
  PSX_TYPE_INVALID = 0,
  PSX_TYPE_VOID,
  PSX_TYPE_BOOL,
  PSX_TYPE_INTEGER,
  PSX_TYPE_FLOAT,
  PSX_TYPE_POINTER,
  PSX_TYPE_ARRAY,
  PSX_TYPE_FUNCTION,
  PSX_TYPE_STRUCT,
  PSX_TYPE_UNION,
  PSX_TYPE_COMPLEX,
} psx_type_kind_t;

typedef struct psx_type_t psx_type_t;
struct psx_type_t {
  psx_type_kind_t kind;
  psx_type_t *base;

  int size;
  int align;
  int elem_size;
  int array_len;

  token_kind_t scalar_kind;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;

  unsigned int is_unsigned : 1;
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_atomic : 1;
  unsigned int is_long_long : 1;
  unsigned int is_plain_char : 1;
  unsigned int is_long_double : 1;
  unsigned int is_vla : 1;

  int deref_size;
  int base_deref_size;
  int pointer_qual_levels;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;

  tk_float_kind_t pointee_fp_kind;
  psx_decl_funcptr_sig_t funcptr_sig;
  char *type_sig;
  int vla_row_stride_frame_off;
  int vla_strides_remaining;
  int ptr_array_pointee_bytes;
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  unsigned char extra_strides_count;

};

psx_type_t *psx_type_new(psx_type_kind_t kind);
psx_type_t *psx_type_new_integer(token_kind_t scalar_kind, int size, int is_unsigned);
psx_type_t *psx_type_new_float(tk_float_kind_t fp_kind, int size);
psx_type_t *psx_type_new_pointer(psx_type_t *base, int deref_size);
psx_type_t *psx_type_wrap_pointer_levels(psx_type_t *base, int levels,
                                          int top_deref_size,
                                          int base_deref_size,
                                          unsigned int const_mask,
                                          unsigned int volatile_mask);
psx_type_t *psx_type_new_array(psx_type_t *base, int array_len, int size, int elem_size, int is_vla);
psx_type_t *psx_type_new_tag(token_kind_t tag_kind, char *tag_name, int tag_len,
                             int tag_scope_depth_p1, int size);

psx_type_kind_t psx_type_kind_from_tag_kind(token_kind_t tag_kind);

int psx_type_sizeof(const psx_type_t *type);
int psx_type_deref_size(const psx_type_t *type);
int psx_type_is_pointer(const psx_type_t *type);
int psx_type_is_unsigned(const psx_type_t *type);
int psx_type_is_scalar(const psx_type_t *type);
int psx_type_is_tag_aggregate(const psx_type_t *type);
int psx_type_pointer_depth(const psx_type_t *type);
int psx_type_pointer_view_qual_levels(const psx_type_t *type);
unsigned int psx_type_pointer_view_qual_mask(const psx_type_t *type,
                                             int is_volatile);
int psx_type_pointer_view_structural_qual_levels(const psx_type_t *type);
unsigned int psx_type_pointer_view_structural_qual_mask(
    const psx_type_t *type, int is_volatile);
int psx_type_pointer_view_base_deref_size(
    const psx_type_t *type, int allow_sizeof_base_fallback);
int psx_type_pointer_view_structural_base_deref_size(const psx_type_t *type);
int psx_type_pointer_view_ptr_array_pointee_bytes(const psx_type_t *type);
int psx_type_pointer_view_structural_ptr_array_pointee_bytes(
    const psx_type_t *type);
int psx_type_carries_ptr_array_pointee_after_deref(const psx_type_t *type);
int psx_type_legacy_flat_pointer_ptr_array_pointee_bytes(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes);
int psx_type_legacy_flat_pointer_stride_matches(
    const psx_type_t *type, int sidecar_outer_stride,
    int sidecar_mid_stride);
int psx_type_pointer_view_ptr_array_pointee_bytes_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes);
int psx_type_pointer_view_array_deref_size_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride);
int psx_type_pointer_view_stride_sync_allowed_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride);
int psx_type_pointer_view_raw_array_shape_allowed(const psx_type_t *type);
int psx_type_pointer_view_raw_stride_copy_allowed(const psx_type_t *type);
int psx_type_pointer_view_raw_array_shape_has_hint(const psx_type_t *type);
int psx_type_pointer_view_raw_array_size_hint(const psx_type_t *type);
int psx_type_copy_pointer_view_stride_metadata(psx_type_t *dst,
                                               const psx_type_t *src);
int psx_type_pointer_view_base_deref_size_with_sidecar(
    const psx_type_t *type, int sidecar_base_deref_size,
    int sidecar_ptr_array_pointee_bytes, int sidecar_outer_stride,
    int sidecar_mid_stride);
int psx_type_pointer_view_quals_with_sidecar(
    const psx_type_t *type, int sidecar_pointer_levels,
    unsigned int sidecar_const_mask, unsigned int sidecar_volatile_mask,
    int sidecar_ptr_array_pointee_bytes, int sidecar_outer_stride,
    int sidecar_mid_stride, int *levels, unsigned int *const_mask,
    unsigned int *volatile_mask);
psx_type_t *psx_type_wrap_ret_pointee_array_base(
    psx_type_t *base, psx_ret_pointee_array_t ret_array);
void psx_type_sync_pointer_to_array_metadata_from_base(psx_type_t *type);
int psx_type_canonicalize_flat_pointer_to_array(psx_type_t *type);
int psx_type_pointer_view_stride_metadata(const psx_type_t *type,
                                          int *inner_stride,
                                          int *next_stride,
                                          int *extra_strides,
                                          int *extra_strides_count);
int psx_type_pointer_view_legacy_stride_metadata(const psx_type_t *type,
                                                 int *inner_stride,
                                                 int *next_stride,
                                                 int *extra_strides,
                                                 int *extra_strides_count);
int psx_type_pointer_view_effective_stride_metadata(const psx_type_t *type,
                                                    int *inner_stride,
                                                    int *next_stride,
                                                    int *extra_strides,
                                                    int *extra_strides_count);
int psx_type_array_view_stride_metadata(const psx_type_t *type,
                                        int keep_outer_row_stride,
                                        int *inner_stride,
                                        int *next_stride,
                                        int *extra_strides,
                                        int *extra_strides_count);
int psx_type_pointer_view_mid_stride(const psx_type_t *type);
int psx_type_pointer_view_outer_stride_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride);
int psx_type_pointer_view_mid_stride_with_sidecar(
    const psx_type_t *type, int sidecar_outer_stride,
    int sidecar_mid_stride);
int psx_type_pointer_view_vla_row_stride_frame_off(const psx_type_t *type);
int psx_type_pointer_view_vla_strides_remaining(const psx_type_t *type);

void psx_type_copy_common_qualifiers(psx_type_t *dst, const psx_type_t *src);
void psx_type_copy_pointer_metadata(psx_type_t *dst, const psx_type_t *src);

#endif
