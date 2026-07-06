#ifndef PARSER_TYPE_H
#define PARSER_TYPE_H

#include "../tokenizer/token.h"

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
  unsigned int is_variadic_func : 1;
  unsigned int returns_void : 1;
  unsigned int returns_data_pointer : 1;
  unsigned int returns_complex : 1;

  int deref_size;
  int base_deref_size;
  int pointer_qual_levels;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;

  tk_float_kind_t pointee_fp_kind;
  tk_float_kind_t funcptr_ret_fp_kind;
  tk_float_kind_t funcptr_ret_pointee_fp_kind;
  int vla_row_stride_frame_off;
  int vla_strides_remaining;
  int ptr_array_pointee_bytes;
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  unsigned char extra_strides_count;

  short funcptr_nargs_fixed;
  unsigned short funcptr_param_fp_mask;
  unsigned short funcptr_param_int_mask;
  unsigned char funcptr_ret_int_width;
  short funcptr_ret_pointee_array_first_dim;
  short funcptr_ret_pointee_array_second_dim;
  short funcptr_ret_pointee_array_elem_size;
};

psx_type_t *psx_type_new(psx_type_kind_t kind);
psx_type_t *psx_type_new_integer(token_kind_t scalar_kind, int size, int is_unsigned);
psx_type_t *psx_type_new_float(tk_float_kind_t fp_kind, int size);
psx_type_t *psx_type_new_pointer(psx_type_t *base, int deref_size);
psx_type_t *psx_type_new_array(psx_type_t *base, int array_len, int size, int elem_size, int is_vla);
psx_type_t *psx_type_new_tag(token_kind_t tag_kind, char *tag_name, int tag_len,
                             int tag_scope_depth_p1, int size);

int psx_type_sizeof(const psx_type_t *type);
int psx_type_deref_size(const psx_type_t *type);
int psx_type_is_pointer(const psx_type_t *type);
int psx_type_is_unsigned(const psx_type_t *type);
int psx_type_is_scalar(const psx_type_t *type);

void psx_type_copy_common_qualifiers(psx_type_t *dst, const psx_type_t *src);
void psx_type_copy_pointer_metadata(psx_type_t *dst, const psx_type_t *src);

#endif
