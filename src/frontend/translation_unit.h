#ifndef FRONTEND_TRANSLATION_UNIT_H
#define FRONTEND_TRANSLATION_UNIT_H

#include "../compilation_session.h"
#include "../hir/hir.h"
#include "../parser/parser.h"

typedef struct {
  psx_parser_stream_t parser;
  psx_toplevel_declaration_callbacks_t toplevel_declarations;
  psx_local_declaration_callbacks_t local_declarations;
  ag_compilation_session_t *session;
  void *compatibility_function;
  unsigned char is_started;
  unsigned char owns_session_activation;
} psx_frontend_stream_t;

typedef struct {
  psx_hir_node_id_t hir_root;
} psx_frontend_function_t;

int psx_frontend_reset_translation_unit_state_in_session(
    ag_compilation_session_t *session);
int psx_frontend_stream_begin(
    psx_frontend_stream_t *stream,
    ag_compilation_session_t *session,
    tokenizer_context_t *tk_ctx, token_t *start);
int psx_frontend_next_function(
    psx_frontend_stream_t *stream, psx_frontend_function_t *function);
int psx_frontend_stream_end(psx_frontend_stream_t *stream);
int psx_frontend_free_processed_ast_in_session(
    ag_compilation_session_t *session);
node_t **psx_frontend_program_in_session(
    ag_compilation_session_t *session,
    tokenizer_context_t *tk_ctx, token_t *start);

#endif
