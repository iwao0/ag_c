#include "type_name_resolution.h"

#include "declaration_application.h"
#include "../parser/declaration_syntax.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"
#include "type_completeness.h"

static psx_scope_lookup_point_t type_name_lookup_point(
    const psx_type_name_ref_t *type_name) {
  return (psx_scope_lookup_point_t){
      .scope_id = type_name ? type_name->scope_seq : 0,
      .declaration_order = type_name ? type_name->declaration_seq : 0,
  };
}

static psx_qual_type_t apply_reference_qualifiers(
    psx_qual_type_t base,
    const psx_parsed_decl_specifier_t *specifier) {
  if (base.type_id == PSX_TYPE_ID_INVALID || !specifier) return base;
  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  if (syntax->is_const_qualified)
    base.qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  if (syntax->is_volatile_qualified)
    base.qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
  if (syntax->is_atomic)
    base.qualifiers |= PSX_TYPE_QUALIFIER_ATOMIC;
  return base;
}

static psx_qual_type_t bind_base_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    const psx_runtime_declarator_application_t **runtime_application) {
  if (runtime_application) *runtime_application = NULL;
  const psx_parsed_type_name_t *syntax = type_name->syntax;
  psx_scope_lookup_point_t point = type_name_lookup_point(type_name);
  if (syntax->atomic_inner) {
    psx_type_name_ref_t inner = {
        .syntax = syntax->atomic_inner,
        .scope_seq = point.scope_id,
        .declaration_seq = point.declaration_order,
    };
    psx_qual_type_t inner_type = {
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    if (!psx_resolve_type_name_qual_type_in_contexts(
            semantic_context, global_registry, local_registry,
            &inner, &inner_type))
      return inner_type;
    return psx_apply_atomic_type_specifier_qual_type_in(
        semantic_context, inner_type, syntax->diagnostic_token);
  }

  const psx_parsed_decl_specifier_t *specifier = &syntax->specifier;
  if (specifier->source == PSX_PARSED_DECL_TYPEDEF_NAME &&
      specifier->typedef_name) {
    psx_typedef_info_t info;
    if (!ps_ctx_find_typedef_name_at_in(
            semantic_context,
            specifier->typedef_name->str,
            specifier->typedef_name->len, point, &info))
      return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                               PSX_TYPE_QUALIFIER_NONE};
    if (runtime_application)
      *runtime_application = info.runtime_application;
    return apply_reference_qualifiers(
        ps_ctx_typedef_decl_qual_type(&info),
        specifier);
  }
  if (specifier->source == PSX_PARSED_DECL_TYPE_TAG &&
      specifier->tag_action.action == PSX_PARSED_TAG_REFERENCE) {
    return apply_reference_qualifiers(
        ps_ctx_tag_qual_type_at_in(
            semantic_context,
            specifier->tag_action.kind,
            specifier->tag_action.name,
            specifier->tag_action.name_len, point),
        specifier);
  }
  return psx_apply_parsed_decl_specifier_qual_type_in_contexts(
      semantic_context, global_registry, local_registry, specifier);
}

int psx_resolve_type_name_base_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_base_resolution_t *resolution) {
  if (resolution) *resolution = (psx_type_name_base_resolution_t){0};
  if (!semantic_context || !global_registry || !local_registry ||
      !type_name || !type_name->syntax || !resolution) return 0;
  const psx_runtime_declarator_application_t *runtime_application = NULL;
  psx_qual_type_t base_qual_type = bind_base_qual_type(
      semantic_context, global_registry, local_registry, type_name,
      &runtime_application);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  *resolution = (psx_type_name_base_resolution_t){
      .base_qual_type = base_qual_type,
      .runtime_application = runtime_application,
  };
  return 1;
}

int psx_resolve_type_name_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_qual_type_t *qual_type) {
  if (qual_type)
    *qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context || !global_registry || !local_registry ||
      !type_name || !type_name->syntax || !qual_type)
    return 0;
  psx_type_name_base_resolution_t base = {0};
  if (!psx_resolve_type_name_base_in_contexts(
          semantic_context, global_registry, local_registry,
          type_name, &base))
    return 0;
  psx_qual_type_t resolved =
      psx_apply_parsed_declarator_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          base.base_qual_type, &type_name->syntax->declarator);
  if (resolved.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  if (psx_semantic_type_has_flexible_array_element_in(
          semantic_context, resolved.type_id)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        type_name->syntax->diagnostic_token, "type-name",
        "a structure or union containing a flexible array member "
        "cannot be an array element");
    return 0;
  }
  *qual_type = resolved;
  return 1;
}
