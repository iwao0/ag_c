#ifndef PARSER_TYPE_NAME_H
#define PARSER_TYPE_NAME_H

#include "type.h"

typedef struct {
  token_kind_t base_kind;
  int base_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;

  unsigned int is_unsigned : 1;
  unsigned int is_long_long : 1;
  unsigned int is_plain_char : 1;
  unsigned int is_long_double : 1;
  unsigned int is_complex : 1;
  unsigned int is_unspecified_array : 1;
  unsigned int pointer_array_element_is_pointer : 1;
  unsigned int canonicalize_function : 1;

  int pointer_levels;
  unsigned int pointer_const_mask;
  unsigned int pointer_volatile_mask;
  int pointer_deref_size;
  int pointer_base_deref_size;
  int pointee_const;
  int pointee_volatile;

  int array_count;
  int array_dims[8];
  int array_dim_count;
  int pointer_array_pointee_bytes;

  psx_decl_funcptr_sig_t funcptr_sig;
  psx_declarator_shape_t declarator_shape;
  const psx_type_t *canonical_base;
  char *type_sig;
} psx_type_name_t;

void psx_type_name_init(psx_type_name_t *name);
psx_type_t *psx_type_name_build(const psx_type_name_t *name);
void psx_type_normalize_integer_identity(psx_type_t *type);

#endif
