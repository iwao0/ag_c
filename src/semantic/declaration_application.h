#ifndef FRONTEND_DECLARATION_APPLICATION_H
#define FRONTEND_DECLARATION_APPLICATION_H

#include "../parser/function_parameter_syntax.h"
#include "../parser/aggregate_member_syntax.h"
#include "../parser/ast.h"
#include "../parser/declarator_shape.h"
#include "../parser/local_registry.h"
#include "declaration_resolution.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_global_registry_t psx_global_registry_t;

typedef enum {
  PSX_DECLARATION_PHASE_EMPTY = 0,
  PSX_DECLARATION_PHASE_SYNTAX,
  PSX_DECLARATION_PHASE_RESOLVED_TYPE,
  PSX_DECLARATION_PHASE_STANDALONE_TAG,
} psx_declaration_phase_state_t;

typedef struct {
  psx_parsed_decl_specifier_t syntax;
  const psx_semantic_type_table_t *type_table;
  psx_qual_type_t base_qual_type;
  int requested_alignment;
  psx_declaration_phase_state_t state;
} psx_declaration_phase_t;

void psx_begin_declaration_phase(
    psx_declaration_phase_t *phase,
    psx_parsed_decl_specifier_t *syntax);
int psx_apply_declaration_phase_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_declaration_phase_t *phase, int standalone_tag);
void psx_dispose_declaration_phase(psx_declaration_phase_t *phase);
psx_qual_type_t psx_declaration_phase_base_qual_type(
    const psx_declaration_phase_t *phase);
const psx_type_t *psx_declaration_phase_base_type(
    const psx_declaration_phase_t *phase);

const psx_type_t *psx_apply_parsed_type_name_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_type_name_t *type_name);
const psx_type_t *psx_apply_parsed_declarator_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_t *base_type,
    const psx_parsed_declarator_t *declarator);
psx_qual_type_t psx_apply_parsed_declarator_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_qual_type_t base_qual_type,
    const psx_parsed_declarator_t *declarator);
const psx_type_t *psx_apply_runtime_declarator_type_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *base_type,
    const psx_runtime_declarator_application_t *application);
psx_qual_type_t psx_apply_runtime_declarator_qual_type_in_context(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t base_qual_type,
    const psx_runtime_declarator_application_t *application);
int psx_compose_runtime_declarator_applications_in(
    arena_context_t *arena_context,
    const psx_runtime_declarator_application_t *declared,
    const psx_runtime_declarator_application_t *typedef_base,
    psx_runtime_declarator_application_t *result);
int psx_resolve_parsed_decl_alignment_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier);
void psx_apply_parsed_declarator_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width);
void psx_apply_runtime_parsed_declarator_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application);
void psx_apply_runtime_parsed_declarator_ex_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index);
void psx_apply_runtime_parsed_declarator_at_lookup_point_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index,
    psx_scope_lookup_point_t lookup_point);
int psx_apply_resolved_runtime_parsed_declarator_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *declarator,
    const psx_runtime_array_bound_t *resolved_bounds,
    int resolved_bound_count,
    psx_runtime_declarator_application_t *application);
void psx_apply_parsed_function_parameters_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token);

int psx_apply_parsed_enum_body_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_enum_body_t *body);
int psx_apply_parsed_aggregate_body_layout_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parsed_aggregate_body_t *body,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *out_size, int *out_align);
const psx_type_t *psx_apply_parsed_decl_specifier_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier);
void psx_apply_parsed_standalone_tag_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_decl_specifier_t *specifier);
#endif
