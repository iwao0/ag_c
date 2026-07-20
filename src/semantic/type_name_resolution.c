#include "type_name_resolution.h"

#include "declaration_resolution.h"
#include "declaration_application.h"
#include "declaration_type_builder.h"
#include "../parser/declaration_syntax.h"
#include "../parser/declarator_shape_builder.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"

static psx_scope_lookup_point_t type_name_lookup_point(
    const psx_type_name_ref_t *type_name) {
  return (psx_scope_lookup_point_t){
      .scope_id = type_name ? type_name->scope_seq : 0,
      .declaration_order = type_name ? type_name->declaration_seq : 0,
  };
}

const psx_type_t *psx_type_name_resolved_type(
    const psx_type_name_resolution_state_t *state) {
  return state && state->kind == PSX_TYPE_NAME_RESOLVED &&
                 state->value.resolved.type_table
             ? psx_semantic_type_table_lookup_qual_type(
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
             ? psx_semantic_type_table_lookup_qual_type(
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
  psx_qual_type_t identity = ps_ctx_intern_qual_type_in(
      semantic_context, resolved_type);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (identity.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_lookup_qual_type(
          semantic_types, identity))
    return 0;
  *state = (psx_type_name_resolution_state_t){
      .kind = PSX_TYPE_NAME_RESOLVED,
      .value.resolved = {
          .type_table = semantic_types,
          .qual_type = identity,
      },
  };
  return 1;
}

static psx_type_t *apply_reference_qualifiers(
    psx_type_t *base, const psx_parsed_decl_specifier_t *specifier) {
  if (!base || !specifier) return base;
  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  ps_type_set_decl_spec_qualifiers(
      base, syntax->is_const_qualified, syntax->is_volatile_qualified);
  if (syntax->is_atomic)
    ps_type_add_qualifiers(base, PSX_TYPE_QUALIFIER_ATOMIC);
  return base;
}

static const psx_type_t *bind_base_type(
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
    psx_type_t *type = ps_type_clone_in(
        ps_ctx_arena(semantic_context),
        psx_resolve_bound_type_name_ref_in_contexts(
            semantic_context, global_registry, local_registry,
            &inner, &inner_state));
    ps_type_add_qualifiers(type, PSX_TYPE_QUALIFIER_ATOMIC);
    return type;
  }

  const psx_parsed_decl_specifier_t *specifier = &syntax->specifier;
  if (specifier->source == PSX_PARSED_DECL_TYPEDEF_NAME &&
      specifier->typedef_name) {
    psx_typedef_info_t info;
    if (!ps_ctx_find_typedef_name_at_in_contexts(
            semantic_context, local_registry,
            specifier->typedef_name->str,
            specifier->typedef_name->len, point, &info))
      return NULL;
    if (runtime_application)
      *runtime_application = info.runtime_application;
    return apply_reference_qualifiers(
        ps_type_clone_in(
            ps_ctx_arena(semantic_context),
            ps_ctx_typedef_decl_type(&info)),
        specifier);
  }
  if (specifier->source == PSX_PARSED_DECL_TYPE_TAG &&
      specifier->tag_action.action == PSX_PARSED_TAG_REFERENCE) {
    psx_type_t *bound = ps_ctx_clone_tag_type_at_in_contexts(
        semantic_context, local_registry,
        specifier->tag_action.kind,
        specifier->tag_action.name,
        specifier->tag_action.name_len, point);
    return apply_reference_qualifiers(bound, specifier);
  }
  return psx_apply_parsed_decl_specifier_in_contexts(
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
      state->kind == PSX_TYPE_NAME_RESOLVED)
    return 1;
  const psx_runtime_declarator_application_t *runtime_application = NULL;
  const psx_type_t *base_type = bind_base_type(
      semantic_context, global_registry, local_registry, type_name,
      &runtime_application);
  if (!base_type) return 0;
  psx_qual_type_t base_qual_type =
      ps_ctx_intern_declaration_qual_type_in(
          semantic_context, base_type);
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
  const psx_type_t *cached = psx_type_name_resolved_type(state);
  if (cached) return cached;
  if (!psx_bind_type_name_ref_in_contexts(
          semantic_context, global_registry, local_registry,
          type_name, state)) return NULL;
  psx_qual_type_t base_qual_type =
      psx_type_name_bound_base_qual_type(state);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) return NULL;
  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator_in_contexts(
      semantic_context, global_registry, local_registry,
      &type_name->syntax->declarator, &shape, NULL);
  psx_type_t *resolved = psx_build_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_qual_type = base_qual_type,
          .declarator_shape = &shape,
      });
  ps_ctx_bind_record_ids_in(semantic_context, resolved);
  if (!psx_type_name_bind_resolved_type_in(
          semantic_context, state, resolved))
    return NULL;
  return psx_type_name_resolved_type(state);
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
  if (!semantic_context || !state || !qual_type) return 0;
  const psx_type_t *resolved =
      psx_resolve_bound_type_name_ref_in_contexts(
          semantic_context, global_registry, local_registry,
          type_name, state);
  if (!resolved) return 0;
  *qual_type = psx_type_name_resolved_qual_type(state);
  return qual_type->type_id != PSX_TYPE_ID_INVALID &&
         ps_ctx_type_by_id_in(
             semantic_context, qual_type->type_id) != NULL;
}
