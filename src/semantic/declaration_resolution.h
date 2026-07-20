#ifndef SEMANTIC_DECLARATION_RESOLUTION_H
#define SEMANTIC_DECLARATION_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/declarator_shape.h"
#include "../parser/declaration_syntax.h"
#include "../parser/type.h"
#include "declarator_application_types.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_qual_type_t base_qual_type;
  const psx_declarator_shape_t *declarator_shape;
} psx_decl_type_request_t;

typedef struct {
  long long initializer_count;
  int entries_initialize_outer_elements;
} psx_incomplete_array_resolution_t;

typedef int (*psx_incomplete_array_constant_index_resolver_t)(
    void *context, const node_t *expression, long long *value);

psx_qual_type_t psx_resolve_decl_qual_type(
    const psx_decl_type_request_t *request);
const psx_type_t *psx_resolve_decl_specifier_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier);
int psx_resolve_incomplete_array_type(
    psx_semantic_context_t *semantic_context, psx_type_t *type,
    const psx_incomplete_array_resolution_t *request);
const psx_type_t *psx_resolve_completed_incomplete_array_type(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *incomplete_type,
    const psx_incomplete_array_resolution_t *request);
int psx_resolve_incomplete_array_initializer_shape(
    const psx_type_t *incomplete_type,
    psx_decl_init_kind_t initializer_kind,
    const node_t *initializer,
    psx_incomplete_array_constant_index_resolver_t resolve_index,
    void *resolve_index_context,
    psx_incomplete_array_resolution_t *resolution);
int psx_resolve_incomplete_array_initializer(
    psx_semantic_context_t *semantic_context, psx_type_t *type,
    psx_decl_init_kind_t initializer_kind,
    node_t *initializer);
int psx_resolve_incomplete_array_initializer_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t incomplete_type,
    psx_decl_init_kind_t initializer_kind,
    node_t *initializer,
    psx_qual_type_t *completed_type);

#endif
