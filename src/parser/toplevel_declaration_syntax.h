#ifndef PARSER_TOPLEVEL_DECLARATION_SYNTAX_H
#define PARSER_TOPLEVEL_DECLARATION_SYNTAX_H

#include "declaration_syntax.h"
#include "initializer_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t *declarators;
  psx_parsed_initializer_t *initializers;
  int declarator_count;
  int is_typedef;
  int is_extern;
  int is_static;
  int is_thread_local;
  int is_standalone_tag;
  token_t *diagnostic_token;
} psx_parsed_toplevel_declaration_t;

typedef struct {
  void *context;
  psx_name_classifier_t name_classifier;
  psx_semantic_context_t *semantic_context;
  psx_parser_runtime_context_t *runtime_context;
  node_t *(*parse_assignment_expression)(void *context);
  void (*record_unsupported_gnu_extension)(
      void *context, const token_t *token, const char *name);
} psx_toplevel_declaration_syntax_context_t;

int psx_parse_toplevel_declaration_syntax_with_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_syntax_context_t *context);
int psx_parse_toplevel_declaration_head_syntax_with_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_syntax_context_t *context);
int psx_finish_toplevel_declaration_syntax_with_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_syntax_context_t *context);
void ps_dispose_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration);

#endif
