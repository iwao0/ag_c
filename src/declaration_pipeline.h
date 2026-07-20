#ifndef DECLARATION_PIPELINE_H
#define DECLARATION_PIPELINE_H

#include "compilation_options.h"
#include "semantic/declaration_application.h"
#include "parser/declaration_syntax.h"
#include "parser/initializer_syntax.h"
#include "parser/symtab.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_vla_runtime_plan_t psx_vla_runtime_plan_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
  char *name;
  int name_len;
  psx_qual_type_t type;
  int is_extern_decl;
  int is_static;
  int is_thread_local;
  int is_compiler_generated;
  const psx_parsed_initializer_t *initializer;
  token_t *diag_tok;
} psx_global_declaration_pipeline_request_t;

typedef struct {
  global_var_t *global;
  int created;
  int initialized;
} psx_global_declaration_pipeline_result_t;

int psx_apply_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result);
int psx_begin_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result);
int psx_finish_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result);

typedef struct {
  psx_semantic_context_t *semantic_context;
  char *name;
  int name_len;
  psx_qual_type_t function_qual_type;
  int is_definition;
  const char *diag_context;
  token_t *diag_tok;
} psx_function_declaration_pipeline_request_t;

int psx_apply_function_declaration_pipeline(
    const psx_function_declaration_pipeline_request_t *request);

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  psx_qual_type_t base_qual_type;
  const psx_parsed_declarator_t *declarator;
} psx_function_definition_pipeline_request_t;

typedef struct {
  struct lvar_t **parameter_vars;
  psx_qual_type_t *parameter_qual_types;
  node_t **args;
  int nargs;
  int has_unnamed_parameter;
  psx_qual_type_t function_qual_type;
} psx_function_definition_pipeline_result_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  psx_qual_type_t base_qual_type;
  psx_runtime_declarator_application_t application;
  psx_function_definition_pipeline_result_t *result;
  int primary_function_op_index;
  int parameter_count;
  int args_capacity;
} psx_function_definition_pipeline_state_t;

int psx_begin_function_definition_pipeline(
    const psx_function_definition_pipeline_request_t *request,
    psx_function_definition_pipeline_result_t *result,
    psx_function_definition_pipeline_state_t *state);
int psx_apply_function_definition_parameter_pipeline(
    psx_function_definition_pipeline_state_t *state,
    const psx_parsed_function_parameter_t *parameter);
int psx_finish_function_definition_pipeline(
    psx_function_definition_pipeline_state_t *state);

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
  char *function_name;
  int function_name_len;
  char *name;
  int name_len;
  psx_qual_type_t type;
  const psx_parsed_initializer_t *initializer;
  token_t *diag_tok;
} psx_static_local_declaration_pipeline_request_t;

typedef struct {
  global_var_t *global;
  struct lvar_t *alias;
  int initialized;
  int type_completed;
} psx_static_local_declaration_pipeline_result_t;

int psx_apply_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result);
int psx_begin_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result);
int psx_finish_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result);
int psx_finish_static_local_declaration_typed_hir_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result,
    const psx_typed_hir_tree_t *initializer_typed_hir);

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  psx_qual_type_t type;
  const psx_runtime_declarator_application_t *application;
  int requested_alignment;
  const psx_parsed_initializer_t *initializer;
  token_t *diag_tok;
} psx_automatic_local_declaration_pipeline_request_t;

typedef struct {
  struct lvar_t *var;
  node_t *initialization;
  psx_vla_runtime_plan_t *vla_runtime_plan;
} psx_automatic_local_declaration_pipeline_result_t;

int psx_apply_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result);
int psx_begin_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result);
int psx_begin_automatic_local_declaration_hir_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result);
int psx_finish_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result);

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
  char *name;
  int name_len;
  psx_qual_type_t type;
  int has_initializer;
  token_t *diag_tok;
} psx_block_extern_declaration_pipeline_request_t;

int psx_apply_block_extern_declaration_pipeline(
    const psx_block_extern_declaration_pipeline_request_t *request);

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  psx_qual_type_t type;
  int requested_alignment;
} psx_temporary_local_declaration_pipeline_request_t;

struct lvar_t *psx_apply_temporary_local_declaration_pipeline(
    const psx_temporary_local_declaration_pipeline_request_t *request);

#endif
