#ifndef SEMANTIC_DECLARATION_RESOLUTION_H
#define SEMANTIC_DECLARATION_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/declaration_syntax.h"
#include "../parser/type.h"

typedef struct {
  token_kind_t base_kind;
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_unsigned;
  int is_complex;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_atomic;
  int is_long_long;
  int is_plain_char;
  int override_plain_char;
  int is_long_double;
  char *typedef_name;
  int typedef_name_len;
  const psx_type_t *base_decl_type;
  const psx_declarator_shape_t *declarator_shape;
} psx_decl_type_request_t;

typedef struct {
  long long initializer_count;
  int entries_initialize_outer_elements;
} psx_incomplete_array_resolution_t;

typedef struct {
  int declarator_op_index;
  node_t *expression;
  long long constant_value;
  int is_constant;
} psx_runtime_array_bound_t;

typedef struct {
  psx_declarator_shape_t shape;
  psx_runtime_array_bound_t array_bounds[24];
  int array_bound_count;
} psx_runtime_declarator_application_t;

psx_type_t *psx_resolve_decl_type(const psx_decl_type_request_t *request);
psx_type_t *psx_resolve_decl_specifier_syntax(
    const psx_parsed_decl_specifier_t *specifier);
int psx_resolve_incomplete_array_type(
    psx_type_t *type, const psx_incomplete_array_resolution_t *request);
int psx_resolve_incomplete_array_initializer(
    psx_type_t *type, psx_decl_init_kind_t initializer_kind,
    node_t *initializer);

#endif
