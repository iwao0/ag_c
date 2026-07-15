#ifndef SEMANTIC_DECLARATION_RESOLUTION_H
#define SEMANTIC_DECLARATION_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/declarator_shape.h"
#include "../parser/declaration_syntax.h"
#include "../parser/type.h"
#include "expression_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  const psx_type_t *base_type;
  const psx_declarator_shape_t *declarator_shape;
} psx_decl_type_request_t;

typedef struct {
  long long initializer_count;
  int entries_initialize_outer_elements;
} psx_incomplete_array_resolution_t;

typedef struct {
  int declarator_op_index;
  psx_semantic_expr_id_t expression_id;
  long long constant_value;
  int is_constant;
} psx_runtime_array_bound_t;

typedef struct {
  psx_declarator_shape_t shape;
  psx_runtime_array_bound_t *array_bounds;
  int array_bound_count;
} psx_runtime_declarator_application_t;

const psx_type_t *psx_resolve_decl_type(
    const psx_decl_type_request_t *request);
const psx_type_t *psx_resolve_decl_specifier_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier);
int psx_resolve_incomplete_array_type(
    psx_semantic_context_t *semantic_context, psx_type_t *type,
    const psx_incomplete_array_resolution_t *request);
int psx_resolve_incomplete_array_initializer(
    psx_semantic_context_t *semantic_context, psx_type_t *type,
    psx_decl_init_kind_t initializer_kind,
    node_t *initializer);

#endif
