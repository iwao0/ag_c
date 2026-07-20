#ifndef FRONTEND_TRANSLATION_UNIT_H
#define FRONTEND_TRANSLATION_UNIT_H

#include "../compilation_session.h"
#include "../hir/hir.h"
#include "../parser/name_environment.h"
#include "../parser/parser.h"
#include "local_declaration.h"

typedef struct {
  psx_parser_stream_t parser;
  psx_frontend_local_declaration_syntax_adapter_t
      local_declaration_adapter;
  psx_local_declaration_callbacks_t local_declarations;
  psx_parser_syntax_services_t parser_syntax;
  psx_parser_name_environment_t local_name_environment;
  ag_compilation_session_t *session;
  unsigned char is_started;
} psx_frontend_stream_t;

typedef struct {
  psx_hir_node_id_t hir_root;
} psx_frontend_function_t;

int psx_frontend_stream_begin(
    psx_frontend_stream_t *stream,
    ag_compilation_session_t *session,
    tokenizer_context_t *tk_ctx, token_t *start);
int psx_frontend_next_function(
    psx_frontend_stream_t *stream, psx_frontend_function_t *function);
int psx_frontend_stream_end(psx_frontend_stream_t *stream);
int psx_frontend_free_processed_ast_in_session(
    ag_compilation_session_t *session);

#endif
