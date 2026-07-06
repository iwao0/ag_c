#ifndef PS_RET_POINTEE_ARRAY_H
#define PS_RET_POINTEE_ARRAY_H

#include <stddef.h>

typedef struct {
  int first_dim;
  int second_dim;
  int elem_size;
} psx_ret_pointee_array_t;

#define psx_ret_pointee_array_make(first_dim, second_dim, elem_size)            \
  ((psx_ret_pointee_array_t){(first_dim), (second_dim), (elem_size)})

#define psx_ret_pointee_array_has_dims(a) ((a).first_dim > 0)

#define psx_ret_pointee_array_equal(a, b)                                      \
  ((a).first_dim == (b).first_dim &&                                           \
   (a).second_dim == (b).second_dim &&                                         \
   (a).elem_size == (b).elem_size)

#define PSX_RET_POINTEE_ARRAY_SELECT_INTO(out, preferred, fallback) do {        \
  const psx_ret_pointee_array_t *_psx_rpa_src =                                \
      ((preferred)->first_dim > 0) ? (preferred) : (fallback);                 \
  (out)->first_dim = _psx_rpa_src->first_dim;                                  \
  (out)->second_dim = _psx_rpa_src->second_dim;                                \
  (out)->elem_size = _psx_rpa_src->elem_size;                                  \
} while (0)

#define psx_ret_pointee_array_row_stride(a)                                    \
  (((a).first_dim <= 0 || (a).elem_size <= 0) ? 0 :                            \
   ((a).first_dim * ((a).second_dim > 0 ? (a).second_dim : 1) * (a).elem_size))

#define psx_ret_pointee_array_inner_stride(a)                                  \
  (((a).first_dim <= 0 || (a).elem_size <= 0) ? 0 :                            \
   (((a).second_dim > 0) ? (a).second_dim * (a).elem_size : (a).elem_size))

#define psx_ret_pointee_array_next_stride(a)                                   \
  (((a).first_dim <= 0 || (a).second_dim <= 0 || (a).elem_size <= 0) ? 0 :      \
   (a).elem_size)

#define psx_ret_pointee_array_strides_from_row(a, row_stride, inner_stride, next_stride) do { \
  psx_ret_pointee_array_t _psx_rpa_a = (a);                                    \
  int _psx_rpa_row = (row_stride);                                             \
  int *_psx_rpa_inner_ptr = (inner_stride);                                    \
  int *_psx_rpa_next_ptr = (next_stride);                                      \
  if (_psx_rpa_inner_ptr) *_psx_rpa_inner_ptr = 0;                             \
  if (_psx_rpa_next_ptr) *_psx_rpa_next_ptr = 0;                               \
  if (_psx_rpa_a.first_dim > 0 && _psx_rpa_row > 0) {                          \
    int _psx_rpa_inner = _psx_rpa_row / _psx_rpa_a.first_dim;                  \
    if (_psx_rpa_inner_ptr) *_psx_rpa_inner_ptr = _psx_rpa_inner;              \
    if (_psx_rpa_a.second_dim > 0 && _psx_rpa_inner > 0 && _psx_rpa_next_ptr) { \
      *_psx_rpa_next_ptr = _psx_rpa_inner / _psx_rpa_a.second_dim;             \
    }                                                                          \
  }                                                                            \
} while (0)

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
