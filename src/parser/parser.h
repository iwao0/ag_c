#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "function_definition_syntax.h"
#include "local_declaration_syntax.h"
#include "name_environment.h"
#include "statement_syntax_context.h"
#include "static_assert_declaration.h"
#include "toplevel_declaration_syntax.h"
#include "../tokenizer/token.h"
#include "../tokenizer/tokenizer.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  void *context;
  psx_parser_runtime_context_t *runtime_context;
  psx_name_classifier_t name_classifier;
  int (*parse_static_assert)(
      void *context,
      psx_parsed_static_assert_declaration_t *assertion,
      const psx_name_classifier_t *name_classifier);
  int (*parse_toplevel_declaration_head)(
      void *context,
      psx_parsed_toplevel_declaration_t *declaration,
      const psx_name_classifier_t *name_classifier);
  int (*finish_toplevel_declaration)(
      void *context,
      psx_parsed_toplevel_declaration_t *declaration,
      const psx_name_classifier_t *name_classifier);
} psx_parser_syntax_services_t;

typedef struct {
  tokenizer_context_t *tk_ctx;
  tokenizer_context_t *previous_runtime_tokenizer_context;
  psx_parser_runtime_context_t *runtime_context;
  psx_parser_syntax_services_t syntax;
  psx_parser_name_environment_t name_environment;
} psx_parser_stream_t;

typedef enum {
  PSX_TOPLEVEL_ITEM_EOF = 0,
  PSX_TOPLEVEL_ITEM_FUNCTION_HEADER,
  PSX_TOPLEVEL_ITEM_STATIC_ASSERT,
  PSX_TOPLEVEL_ITEM_DECLARATION,
} psx_toplevel_item_kind_t;

typedef struct {
  psx_toplevel_item_kind_t kind;
  union {
    psx_parsed_function_definition_t function_header;
    psx_parsed_static_assert_declaration_t static_assertion;
    psx_parsed_toplevel_declaration_t declaration;
  } value;
} psx_parsed_toplevel_item_t;

// トップレベル項目のストリーミングパース。frontendはitemを逐次適用する。
// 1 関数ぶんの AST だけを保持して codegen→解放できるので、AST のピークメモリを抑える。
void ps_parser_stream_begin_with_syntax(
    psx_parser_stream_t *stream,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_parser_syntax_services_t *syntax);
int ps_parse_next_toplevel_item(
    psx_parser_stream_t *stream, psx_parsed_toplevel_item_t *item);
int ps_parse_function_definition_body(
    psx_parser_stream_t *stream,
    psx_parsed_function_definition_t *definition,
    const psx_statement_syntax_context_t *statement_syntax);
void ps_parser_stream_end(psx_parser_stream_t *stream);

#endif
