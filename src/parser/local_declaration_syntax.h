#ifndef PARSER_LOCAL_DECLARATION_SYNTAX_H
#define PARSER_LOCAL_DECLARATION_SYNTAX_H

#include "ast.h"
#include "declaration_syntax.h"
#include "initializer_syntax.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct psx_parsed_local_declaration_t {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t *declarators;
  psx_parsed_initializer_t *initializers;
  int declarator_count;
  int is_typedef;
  int is_extern;
  int is_static;
  int is_standalone_tag;
  token_t *diagnostic_token;
} psx_parsed_local_declaration_t;

typedef struct psx_local_declaration_callbacks_t {
  void *context;
  psx_name_classifier_t name_classifier;
  psx_parser_runtime_context_t *runtime_context;
  node_t *(*parse_static_assert)(void *context);
  int (*parse_decl_specifier)(
      void *context, psx_parsed_decl_specifier_t *specifier);
  void (*parse_declarator)(
      void *context, psx_parsed_declarator_t *declarator);
  void (*parse_runtime_declarator_expressions)(
      void *context, psx_parsed_declarator_t *declarator);
  void (*parse_initializer)(
      void *context, psx_parsed_initializer_t *initializer,
      token_t *assign_tok);
} psx_local_declaration_callbacks_t;

node_t *psx_parse_local_declaration_syntax(
    const psx_local_declaration_callbacks_t *callbacks);

#endif
