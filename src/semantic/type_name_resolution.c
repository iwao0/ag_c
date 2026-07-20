#include "type_name_resolution.h"

#include "declaration_application.h"
#include "type_compatibility_view.h"
#include "../parser/declaration_syntax.h"
#include "../parser/semantic_ctx.h"

static psx_scope_lookup_point_t type_name_lookup_point(
    const psx_type_name_ref_t *type_name) {
  return (psx_scope_lookup_point_t){
      .scope_id = type_name ? type_name->scope_seq : 0,
      .declaration_order = type_name ? type_name->declaration_seq : 0,
  };
}

static int type_name_qual_type_is_valid(
    const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t type) {
  const psx_type_qualifiers_t supported =
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE |
      PSX_TYPE_QUALIFIER_ATOMIC;
  psx_type_shape_t shape = {0};
  return (type.qualifiers & ~supported) == 0 &&
         psx_semantic_type_table_describe(
             semantic_types, type.type_id, &shape);
}

const psx_type_t *psx_type_name_resolved_type(
    const psx_type_name_resolution_state_t *state) {
  return state && state->kind == PSX_TYPE_NAME_RESOLVED &&
                 state->value.resolved.type_table
             ? psx_type_compatibility_view_for(
                   state->value.resolved.type_table,
                   state->value.resolved.qual_type)
             : NULL;
}

psx_qual_type_t psx_type_name_resolved_qual_type(
    const psx_type_name_resolution_state_t *state) {
  return state && state->kind == PSX_TYPE_NAME_RESOLVED
             ? state->value.resolved.qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

const psx_type_t *psx_type_name_bound_base_type(
    const psx_type_name_resolution_state_t *state) {
  return state && state->kind == PSX_TYPE_NAME_BOUND
             ? psx_type_compatibility_view_for(
                   state->value.bound.type_table,
                   state->value.bound.base_qual_type)
             : NULL;
}

psx_qual_type_t psx_type_name_bound_base_qual_type(
    const psx_type_name_resolution_state_t *state) {
  return state && state->kind == PSX_TYPE_NAME_BOUND
             ? state->value.bound.base_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

const psx_runtime_declarator_application_t *
psx_type_name_bound_runtime_application(
    const psx_type_name_resolution_state_t *state) {
  return state && state->kind == PSX_TYPE_NAME_BOUND
             ? state->value.bound.runtime_application : NULL;
}

int psx_type_name_bind_resolved_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_name_resolution_state_t *state,
    const psx_type_t *resolved_type) {
  if (!semantic_context || !state || !resolved_type) return 0;
  return psx_type_name_bind_resolved_qual_type_in(
      semantic_context, state,
      ps_ctx_intern_qual_type_in(semantic_context, resolved_type));
}

int psx_type_name_bind_resolved_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_name_resolution_state_t *state,
    psx_qual_type_t resolved_type) {
  if (!semantic_context || !state ||
      resolved_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (!type_name_qual_type_is_valid(
          semantic_types, resolved_type))
    return 0;
  *state = (psx_type_name_resolution_state_t){
      .kind = PSX_TYPE_NAME_RESOLVED,
      .value.resolved = {
          .type_table = semantic_types,
          .qual_type = resolved_type,
      },
  };
  return 1;
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
    psx_type_name_resolution_state_t inner_state = {0};
    psx_qual_type_t inner_type = {
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    if (!psx_resolve_bound_type_name_qual_type_in_contexts(
            semantic_context, global_registry, local_registry,
            &inner, &inner_state, &inner_type))
      return inner_type;
    inner_type.qualifiers |= PSX_TYPE_QUALIFIER_ATOMIC;
    return inner_type;
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

int psx_bind_type_name_ref_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state) {
  if (!semantic_context || !global_registry || !local_registry ||
      !type_name || !type_name->syntax || !state) return 0;
  if (state->kind == PSX_TYPE_NAME_BOUND ||
      state->kind == PSX_TYPE_NAME_RESOLVED) {
    const psx_semantic_type_table_t *bound_types =
        state->kind == PSX_TYPE_NAME_BOUND
            ? state->value.bound.type_table
            : state->value.resolved.type_table;
    return bound_types ==
           ps_ctx_semantic_type_table_in(semantic_context);
  }
  const psx_runtime_declarator_application_t *runtime_application = NULL;
  psx_qual_type_t base_qual_type = bind_base_qual_type(
      semantic_context, global_registry, local_registry, type_name,
      &runtime_application);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  *state = (psx_type_name_resolution_state_t){
      .kind = PSX_TYPE_NAME_BOUND,
      .value.bound = {
          .type_table = ps_ctx_semantic_type_table_in(
              semantic_context),
          .base_qual_type = base_qual_type,
          .runtime_application = runtime_application,
      },
  };
  return 1;
}

const psx_type_t *psx_resolve_bound_type_name_ref_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state) {
  if (!semantic_context || !global_registry || !local_registry ||
      !type_name || !state)
    return NULL;
  psx_qual_type_t resolved = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!psx_resolve_bound_type_name_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          type_name, state, &resolved))
    return NULL;
  return psx_type_compatibility_view_for(
      ps_ctx_semantic_type_table_in(semantic_context), resolved);
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
  psx_type_name_resolution_state_t state = {0};
  return psx_resolve_bound_type_name_qual_type_in_contexts(
      semantic_context, global_registry, local_registry,
      type_name, &state, qual_type);
}

int psx_resolve_bound_type_name_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state,
    psx_qual_type_t *qual_type) {
  if (qual_type)
    *qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context || !global_registry || !local_registry ||
      !type_name || !type_name->syntax || !state || !qual_type)
    return 0;
  psx_qual_type_t cached = psx_type_name_resolved_qual_type(state);
  if (cached.type_id != PSX_TYPE_ID_INVALID) {
    const psx_semantic_type_table_t *semantic_types =
        ps_ctx_semantic_type_table_in(semantic_context);
    if (state->value.resolved.type_table != semantic_types ||
        !type_name_qual_type_is_valid(semantic_types, cached))
      return 0;
    *qual_type = cached;
    return 1;
  }
  if (!psx_bind_type_name_ref_in_contexts(
          semantic_context, global_registry, local_registry,
          type_name, state))
    return 0;
  psx_qual_type_t base_qual_type =
      psx_type_name_bound_base_qual_type(state);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  psx_qual_type_t resolved =
      psx_apply_parsed_declarator_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          base_qual_type, &type_name->syntax->declarator);
  if (!psx_type_name_bind_resolved_qual_type_in(
          semantic_context, state, resolved))
    return 0;
  *qual_type = resolved;
  return 1;
}
