#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "function_definition_syntax.h"
#include "local_declaration_syntax.h"
#include "static_assert_declaration.h"
#include "toplevel_declaration_syntax.h"
#include "../tokenizer/token.h"
#include "../tokenizer/tokenizer.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef struct {
  tokenizer_context_t *tk_ctx;
  psx_semantic_context_t *semantic_context;
  psx_local_registry_t *local_registry;
  const psx_toplevel_declaration_callbacks_t *toplevel_declarations;
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
void ps_parser_stream_begin(
    psx_parser_stream_t *stream,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations);
void ps_parser_stream_begin_in_contexts(
    psx_parser_stream_t *stream,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations);
int ps_parse_next_toplevel_item(
    psx_parser_stream_t *stream, psx_parsed_toplevel_item_t *item);
node_t *ps_parse_function_definition_body(
    psx_parser_stream_t *stream, node_function_definition_t *function,
    const psx_local_declaration_callbacks_t *local_declarations);
void ps_parser_stream_end(psx_parser_stream_t *stream);

// 単一の式をパースしてASTのルートを返す
node_t *ps_expr(void);
// 先頭トークンを明示指定して単一式をパースする
node_t *ps_expr_from(token_t *start);
// Tokenizerコンテキストを明示して単一式をパースする
node_t *ps_expr_ctx(tokenizer_context_t *tk_ctx, token_t *start);

#endif
