#include "type_name_resolution.h"

#include "declaration_resolution.h"
#include "../frontend/declaration_application.h"
#include "../parser/declaration_syntax.h"

int psx_bind_type_name_ref(psx_type_name_ref_t *type_name) {
  if (!type_name || !type_name->syntax) return 0;
  if (type_name->bound_base_type) return 1;
  psx_parsed_type_name_t *syntax = type_name->syntax;
  if (syntax->atomic_inner) {
    ps_prepare_constant_declarator_expressions(
        &syntax->atomic_inner->declarator);
    type_name->bound_base_type =
        psx_apply_parsed_type_name(syntax->atomic_inner);
    if (type_name->bound_base_type)
      type_name->bound_base_type->is_atomic = 1;
  } else {
    type_name->bound_base_type =
        psx_apply_parsed_decl_specifier(&syntax->specifier);
  }
  return type_name->bound_base_type != NULL;
}

psx_type_t *psx_resolve_bound_type_name_ref(
    psx_type_name_ref_t *type_name) {
  if (!type_name) return NULL;
  if (type_name->resolved_type) return type_name->resolved_type;
  if (!psx_bind_type_name_ref(type_name)) return NULL;
  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator(
      &type_name->syntax->declarator, &shape, NULL);
  type_name->resolved_type = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_decl_type = type_name->bound_base_type,
          .declarator_shape = &shape,
      });
  return type_name->resolved_type;
}
