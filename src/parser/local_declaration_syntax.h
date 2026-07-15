#ifndef PARSER_LOCAL_DECLARATION_SYNTAX_H
#define PARSER_LOCAL_DECLARATION_SYNTAX_H

#include "ast.h"
#include "declaration_syntax.h"
#include "initializer_syntax.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct ag_compilation_options_t ag_compilation_options_t;

typedef struct psx_local_declaration_callbacks_t {
  void *context;
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_parser_runtime_context_t *runtime_context;
  const ag_compilation_options_t *options;
  void (*apply_static_assert)(
      void *context, node_t *condition, token_t *diagnostic_token);
  void *(*begin_declaration)(
      void *context, const psx_parsed_decl_specifier_t *specifier,
      int is_typedef, int is_standalone_tag);
  void (*begin_declarator)(
      void *declaration_context,
      const psx_parsed_declarator_t *declarator,
      const psx_parsed_initializer_t *initializer);
  void (*finish_declarator)(
      void *declaration_context,
      const psx_parsed_initializer_t *initializer);
  node_t *(*finish_declaration)(void *declaration_context);
  void (*abort_declaration)(void *declaration_context);
} psx_local_declaration_callbacks_t;

node_t *psx_parse_local_declaration_syntax(
    const psx_local_declaration_callbacks_t *callbacks);

#endif
