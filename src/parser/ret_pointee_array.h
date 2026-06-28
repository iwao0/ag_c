#ifndef PS_RET_POINTEE_ARRAY_H
#define PS_RET_POINTEE_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  int first_dim;
  int second_dim;
  int elem_size;
} psx_ret_pointee_array_t;

static inline psx_ret_pointee_array_t psx_ret_pointee_array_make(int first_dim,
                                                                  int second_dim,
                                                                  int elem_size) {
  psx_ret_pointee_array_t out = {first_dim, second_dim, elem_size};
  return out;
}

static inline bool psx_ret_pointee_array_has_dims(psx_ret_pointee_array_t a) {
  return a.first_dim > 0;
}

static inline psx_ret_pointee_array_t psx_ret_pointee_array_select(psx_ret_pointee_array_t preferred,
                                                                    psx_ret_pointee_array_t fallback) {
  return psx_ret_pointee_array_has_dims(preferred) ? preferred : fallback;
}

static inline void psx_ret_pointee_array_store_shorts(psx_ret_pointee_array_t a,
                                                      short *first_dim,
                                                      short *second_dim,
                                                      short *elem_size) {
  if (first_dim) *first_dim = (short)a.first_dim;
  if (second_dim) *second_dim = (short)a.second_dim;
  if (elem_size) *elem_size = (short)a.elem_size;
}

static inline int psx_ret_pointee_array_row_stride(psx_ret_pointee_array_t a) {
  if (a.first_dim <= 0 || a.elem_size <= 0) return 0;
  return a.first_dim * (a.second_dim > 0 ? a.second_dim : 1) * a.elem_size;
}

static inline int psx_ret_pointee_array_inner_stride(psx_ret_pointee_array_t a) {
  if (a.first_dim <= 0 || a.elem_size <= 0) return 0;
  return (a.second_dim > 0) ? a.second_dim * a.elem_size : a.elem_size;
}

static inline int psx_ret_pointee_array_next_stride(psx_ret_pointee_array_t a) {
  if (a.first_dim <= 0 || a.second_dim <= 0 || a.elem_size <= 0) return 0;
  return a.elem_size;
}

static inline void psx_ret_pointee_array_strides_from_row(psx_ret_pointee_array_t a,
                                                          int row_stride,
                                                          int *inner_stride,
                                                          int *next_stride) {
  if (inner_stride) *inner_stride = 0;
  if (next_stride) *next_stride = 0;
  if (a.first_dim <= 0 || row_stride <= 0) return;
  int inner = row_stride / a.first_dim;
  if (inner_stride) *inner_stride = inner;
  if (a.second_dim > 0 && inner > 0 && next_stride) {
    *next_stride = inner / a.second_dim;
  }
}

static inline void psx_ret_pointee_array_absorb_suffix(int *arr_is_array,
                                                       int *arr_total,
                                                       int *dim_count,
                                                       int *first_dim,
                                                       int *dims,
                                                       int dims_cap,
                                                       int elem_size,
                                                       psx_ret_pointee_array_t *out) {
  if (out) {
    *out = psx_ret_pointee_array_make(first_dim ? *first_dim : 0,
                                      (dim_count && *dim_count >= 2 && dims) ? dims[1] : 0,
                                      elem_size);
  }
  if (arr_is_array) *arr_is_array = 0;
  if (arr_total) *arr_total = 1;
  if (dim_count) *dim_count = 0;
  if (first_dim) *first_dim = 0;
  for (int i = 0; dims && i < dims_cap; i++) dims[i] = 0;
}

static inline void psx_ret_pointee_array_absorb_member_suffix(int *arr_size,
                                                              int *dim_count,
                                                              int *first_dim,
                                                              int *dims,
                                                              int dims_cap,
                                                              int elem_size,
                                                              psx_ret_pointee_array_t *out) {
  psx_ret_pointee_array_absorb_suffix(NULL, arr_size, dim_count, first_dim, dims,
                                      dims_cap, elem_size, out);
}

#endif
