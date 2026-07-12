#ifndef SEMANTIC_DECLARATION_APPLICATION_H
#define SEMANTIC_DECLARATION_APPLICATION_H

#include "../parser/function_parameter_syntax.h"
#include "../parser/ast.h"
#include "aggregate_member_resolution.h"
#include "tag_declaration_resolution.h"

typedef enum {
  PSX_DECLARATION_PHASE_EMPTY = 0,
  PSX_DECLARATION_PHASE_SYNTAX,
  PSX_DECLARATION_PHASE_RESOLVED_TYPE,
  PSX_DECLARATION_PHASE_STANDALONE_TAG,
} psx_declaration_phase_state_t;

typedef struct {
  psx_parsed_decl_specifier_t syntax;
  psx_type_t *base_type;
  int requested_alignment;
  psx_declaration_phase_state_t state;
} psx_declaration_phase_t;

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

void psx_parse_declaration_phase_syntax(
    psx_declaration_phase_t *phase,
    const psx_decl_specifier_syntax_options_t *options);
int psx_apply_declaration_phase(
    psx_declaration_phase_t *phase, int standalone_tag);
void psx_dispose_declaration_phase(psx_declaration_phase_t *phase);

psx_type_t *psx_apply_parsed_decl_specifier(
    const psx_parsed_decl_specifier_t *specifier);
psx_type_t *psx_apply_parsed_type_name(
    const psx_parsed_type_name_t *type_name);
psx_type_t *psx_apply_parsed_declarator_type(
    const psx_type_t *base_type,
    const psx_parsed_declarator_t *declarator);
psx_type_t *psx_apply_runtime_declarator_type(
    const psx_type_t *base_type,
    const psx_runtime_declarator_application_t *application);
int psx_apply_parsed_decl_alignment(
    const psx_parsed_decl_specifier_t *specifier);
void psx_apply_parsed_standalone_tag(
    const psx_parsed_decl_specifier_t *specifier);
void psx_apply_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width);
void psx_apply_runtime_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application);
void psx_apply_runtime_parsed_declarator_ex(
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index);
void psx_apply_parsed_function_parameters(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token);

void psx_apply_parsed_typedef_declaration(
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok);
void psx_apply_parsed_enum_constant(
    char *name, int name_len, long long value, token_t *diag_tok);
void psx_apply_parsed_tag_declaration(
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok);
int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok);
void psx_apply_static_assert(node_t *condition, token_t *diag_tok);

#endif
