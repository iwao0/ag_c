#ifndef FRONTEND_TRANSLATION_UNIT_H
#define FRONTEND_TRANSLATION_UNIT_H

#include "../compiler_context.h"
#include "../parser/parser.h"

typedef struct {
  psx_parser_stream_t parser;
  psx_toplevel_declaration_callbacks_t toplevel_declarations;
  psx_local_declaration_callbacks_t local_declarations;
  ag_compiler_context_t *compiler_context;
  unsigned char is_started;
} psx_frontend_stream_t;

void psx_frontend_reset_translation_unit_state(void);
int psx_frontend_reset_translation_unit_state_in_compiler_context(
    ag_compiler_context_t *compiler_context);
int psx_frontend_stream_begin(
    psx_frontend_stream_t *stream,
    ag_compiler_context_t *compiler_context,
    tokenizer_context_t *tk_ctx, token_t *start);
node_t *psx_frontend_next_function(psx_frontend_stream_t *stream);
void psx_frontend_stream_end(psx_frontend_stream_t *stream);
void psx_frontend_free_processed_ast(void);
node_t **psx_frontend_program(void);
node_t **psx_frontend_program_from(token_t *start);
node_t **psx_frontend_program_in_compiler_context(
    ag_compiler_context_t *compiler_context,
    tokenizer_context_t *tk_ctx, token_t *start);
node_t **psx_frontend_program_ctx(
    tokenizer_context_t *tk_ctx, token_t *start);

#endif
