#include "translation_unit.h"

#include "declaration_registration.h"
#include "function_definition.h"
#include "local_declaration.h"
#include "toplevel_declaration.h"
#include "../parser/anon_tag.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/expr.h"
#include "../parser/global_registry.h"
#include "../pragma_pack.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/semantic_pass.h"
#include <stdlib.h>

void psx_frontend_reset_translation_unit_state(void) {
  ps_global_registry_reset_translation_unit();
  psx_anon_tag_reset_translation_unit_state();
  psx_expr_reset_translation_unit_state();
  psx_decl_reset_translation_unit_state();
  psx_ctx_reset_translation_unit_scope();
  pragma_pack_reset();
  arena_free_all();
}

void psx_frontend_stream_begin(
    psx_frontend_stream_t *stream,
    tokenizer_context_t *tk_ctx, token_t *start) {
  if (!stream) return;
  psx_global_registry_reset_diag_state();
  ps_ctx_reset_function_diag_state();
  ps_ctx_reset_tag_diag_state();
  psx_ctx_reset_function_names();
  psx_parser_stream_begin(
      &stream->parser, tk_ctx, start,
      psx_frontend_toplevel_declaration_callbacks());
}

node_t *psx_frontend_next_function(psx_frontend_stream_t *stream) {
  if (!stream) return NULL;
  psx_parsed_toplevel_item_t item;
  while (ps_parse_next_toplevel_item(&stream->parser, &item)) {
    if (item.kind == PSX_TOPLEVEL_ITEM_STATIC_ASSERT) {
      psx_apply_static_assert(
          item.value.static_assertion.condition,
          item.value.static_assertion.diagnostic_token);
      continue;
    }
    if (item.kind == PSX_TOPLEVEL_ITEM_DECLARATION) {
      psx_apply_toplevel_declaration(&item.value.declaration);
      ps_dispose_toplevel_declaration_syntax(
          &item.value.declaration);
      continue;
    }
    if (item.kind == PSX_TOPLEVEL_ITEM_FUNCTION_HEADER) {
      node_func_t *header = psx_apply_function_definition_header(
          &item.value.function_header);
      node_t *function = ps_parse_function_definition_body(
          &stream->parser, header,
          psx_frontend_local_declaration_callbacks());
      ps_dispose_function_definition_header_syntax(
          &item.value.function_header);
      psx_semantic_analyze_function(function, function->tok);
      return function;
    }
  }
  return NULL;
}

void psx_frontend_stream_end(psx_frontend_stream_t *stream) {
  if (!stream) return;
  psx_ctx_emit_deferred_parser_warnings();
  psx_parser_stream_end(&stream->parser);
}

void psx_frontend_free_processed_ast(void) {
  arena_free_all();
}

node_t **psx_frontend_program_ctx(
    tokenizer_context_t *tk_ctx, token_t *start) {
  psx_frontend_stream_t stream = {0};
  psx_frontend_stream_begin(&stream, tk_ctx, start);
  int capacity = 16;
  int count = 0;
  node_t **program = calloc((size_t)capacity, sizeof(*program));
  if (!program) {
    psx_frontend_stream_end(&stream);
    return NULL;
  }
  for (node_t *function;
       (function = psx_frontend_next_function(&stream)) != NULL;) {
    if (count >= capacity - 1) {
      capacity *= 2;
      node_t **grown = realloc(
          program, (size_t)capacity * sizeof(*program));
      if (!grown) {
        free(program);
        psx_frontend_stream_end(&stream);
        return NULL;
      }
      program = grown;
    }
    program[count++] = function;
  }
  program[count] = NULL;
  psx_frontend_stream_end(&stream);
  psx_semantic_analyze_program(program);
  return program;
}

node_t **psx_frontend_program_from(token_t *start) {
  return psx_frontend_program_ctx(NULL, start);
}

node_t **psx_frontend_program(void) {
  return psx_frontend_program_ctx(NULL, tk_get_current_token());
}
