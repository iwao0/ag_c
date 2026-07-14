#include "type_name_resolution.h"

#include "declaration_resolution.h"
#include "declaration_application.h"
#include "../parser/declaration_syntax.h"
#include "../parser/semantic_ctx.h"

static psx_local_lookup_point_t type_name_lookup_point(
    const psx_type_name_ref_t *type_name) {
  return (psx_local_lookup_point_t){
      .scope_seq = type_name ? type_name->scope_seq : 0,
      .declaration_seq = type_name ? type_name->declaration_seq : 0,
  };
}

static psx_type_t *apply_reference_qualifiers(
    psx_type_t *base, const psx_parsed_decl_specifier_t *specifier) {
  if (!base || !specifier) return base;
  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  ps_type_set_decl_spec_qualifiers(
      base, syntax->is_const_qualified, syntax->is_volatile_qualified);
  if (syntax->is_atomic) base->is_atomic = 1;
  return base;
}

static psx_type_t *bind_base_type(
    psx_type_name_ref_t *type_name) {
  psx_parsed_type_name_t *syntax = type_name->syntax;
  psx_local_lookup_point_t point = type_name_lookup_point(type_name);
  if (syntax->atomic_inner) {
    psx_type_name_ref_t inner = {
        .syntax = syntax->atomic_inner,
        .scope_seq = point.scope_seq,
        .declaration_seq = point.declaration_seq,
    };
    psx_type_t *type = psx_resolve_bound_type_name_ref(&inner);
    if (type) type->is_atomic = 1;
    return type;
  }

  const psx_parsed_decl_specifier_t *specifier = &syntax->specifier;
  if (specifier->source == PSX_PARSED_DECL_TYPEDEF_NAME &&
      specifier->typedef_name) {
    const psx_type_t *bound = NULL;
    if (!ps_ctx_find_typedef_decl_type_at(
            specifier->typedef_name->str,
            specifier->typedef_name->len, point, &bound))
      return NULL;
    return apply_reference_qualifiers(
        ps_type_clone(bound), specifier);
  }
  if (specifier->source == PSX_PARSED_DECL_TYPE_TAG &&
      specifier->tag_action.action == PSX_PARSED_TAG_REFERENCE) {
    psx_type_t *bound = ps_ctx_clone_tag_type_at(
        specifier->tag_action.kind,
        specifier->tag_action.name,
        specifier->tag_action.name_len, point);
    return apply_reference_qualifiers(bound, specifier);
  }
  return psx_apply_parsed_decl_specifier(specifier);
}

int psx_bind_type_name_ref(psx_type_name_ref_t *type_name) {
  if (!type_name || !type_name->syntax) return 0;
  if (type_name->bound_base_type) return 1;
  type_name->bound_base_type = bind_base_type(type_name);
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
          .base_type = type_name->bound_base_type,
          .declarator_shape = &shape,
      });
  return type_name->resolved_type;
}
