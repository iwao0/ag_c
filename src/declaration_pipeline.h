#ifndef DECLARATION_PIPELINE_H
#define DECLARATION_PIPELINE_H

#include "semantic/declaration_application.h"
#include "parser/declaration_syntax.h"
#include "parser/initializer_syntax.h"
#include "parser/symtab.h"

typedef void (*psx_parse_declaration_initializer_fn)(
    void *context, psx_type_t *type,
    psx_parsed_initializer_t *initializer);

typedef struct {
  char *name;
  int name_len;
  psx_type_t *type;
  int is_extern_decl;
  int is_static;
  int is_thread_local;
  psx_parsed_initializer_t *initializer;
  psx_parse_declaration_initializer_fn parse_initializer;
  void *parse_context;
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

typedef struct {
  char *name;
  int name_len;
  const psx_type_t *return_type;
  psx_type_t *const *parameter_types;
  int parameter_count;
  int is_variadic;
  int is_definition;
  node_func_t *function_node;
  const char *diag_context;
  token_t *diag_tok;
} psx_function_declaration_pipeline_request_t;

int psx_apply_function_declaration_pipeline(
    const psx_function_declaration_pipeline_request_t *request);

typedef struct {
  const psx_type_t *base_type;
  psx_parsed_declarator_t *declarator;
} psx_function_definition_pipeline_request_t;

typedef struct {
  node_t **args;
  int nargs;
  int is_variadic;
  int has_unnamed_parameter;
  psx_type_t *function_type;
  psx_type_t *return_type;
} psx_function_definition_pipeline_result_t;

int psx_apply_function_definition_pipeline(
    const psx_function_definition_pipeline_request_t *request,
    psx_function_definition_pipeline_result_t *result);

typedef struct {
  char *function_name;
  int function_name_len;
  char *name;
  int name_len;
  psx_type_t *type;
  psx_parsed_initializer_t *initializer;
  psx_parse_declaration_initializer_fn parse_initializer;
  void *parse_context;
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

typedef struct {
  char *name;
  int name_len;
  psx_type_t *type;
  const psx_runtime_declarator_application_t *application;
  int requested_alignment;
  psx_parsed_initializer_t *initializer;
  psx_parse_declaration_initializer_fn parse_initializer;
  void *parse_context;
  token_t *diag_tok;
} psx_automatic_local_declaration_pipeline_request_t;

typedef struct {
  struct lvar_t *var;
  node_t *initialization;
  int type_attached;
} psx_automatic_local_declaration_pipeline_result_t;

int psx_apply_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result);

typedef struct {
  char *name;
  int name_len;
  psx_type_t *type;
  int has_initializer;
  token_t *diag_tok;
} psx_block_extern_declaration_pipeline_request_t;

typedef struct {
  global_var_t *global;
  int is_function;
} psx_block_extern_declaration_pipeline_result_t;

int psx_apply_block_extern_declaration_pipeline(
    const psx_block_extern_declaration_pipeline_request_t *request,
    psx_block_extern_declaration_pipeline_result_t *result);

typedef struct {
  char *name;
  int name_len;
  psx_type_t *type;
  int requested_alignment;
} psx_temporary_local_declaration_pipeline_request_t;

struct lvar_t *psx_apply_temporary_local_declaration_pipeline(
    const psx_temporary_local_declaration_pipeline_request_t *request);

void psx_declaration_pipeline_reset_translation_unit_state(void);

#endif
