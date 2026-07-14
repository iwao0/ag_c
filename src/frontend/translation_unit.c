#include "translation_unit.h"

#include "../diag/diag.h"
#include "../semantic/declaration_registration.h"
#include "function_definition.h"
#include "local_declaration.h"
#include "semantic_pipeline.h"
#include "toplevel_declaration.h"
#include "../parser/anon_tag.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/expr.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../pragma_pack.h"
#include "../parser/semantic_ctx.h"
#include <stdlib.h>

static void reset_translation_unit_state(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry) {
  ps_global_registry_reset_translation_unit_in(global_registry);
  ps_anon_tag_reset_translation_unit_state();
  ps_expr_reset_translation_unit_state();
  ps_decl_reset_translation_unit_state_in(local_registry);
  ps_ctx_reset_translation_unit_scope_in(semantic_context);
  pragma_pack_reset();
  arena_free_all();
}

void psx_frontend_reset_translation_unit_state(void) {
  reset_translation_unit_state(
      ps_ctx_active(), ps_global_registry_active(),
      ps_local_registry_active());
}

void psx_frontend_reset_translation_unit_state_in_context(
    psx_semantic_context_t *semantic_context) {
  reset_translation_unit_state(
      semantic_context, ps_global_registry_active(),
      ps_local_registry_active());
}

void psx_frontend_reset_translation_unit_state_in_compiler_context(
    ag_compiler_context_t *compiler_context) {
  if (!compiler_context) {
    psx_frontend_reset_translation_unit_state();
    return;
  }
  reset_translation_unit_state(
      compiler_context->semantic_context,
      compiler_context->global_registry,
      compiler_context->local_registry);
}

void psx_frontend_stream_begin(
    psx_frontend_stream_t *stream,
    ag_compiler_context_t *compiler_context,
    tokenizer_context_t *tk_ctx, token_t *start) {
  if (!stream) return;
  stream->compiler_context = compiler_context;
  psx_semantic_context_t *semantic_context = compiler_context
      ? compiler_context->semantic_context : ps_ctx_active();
  ps_global_registry_reset_diag_state_in(
      compiler_context
          ? compiler_context->global_registry
          : ps_global_registry_active());
  ps_ctx_reset_function_diag_state_in(semantic_context);
  ps_ctx_reset_tag_diag_state_in(semantic_context);
  ps_ctx_reset_function_names_in(semantic_context);
  psx_frontend_init_toplevel_declaration_callbacks_in_contexts(
      &stream->toplevel_declarations, semantic_context,
      compiler_context
          ? compiler_context->global_registry
          : ps_global_registry_active(),
      compiler_context
          ? compiler_context->local_registry
          : ps_local_registry_active());
  psx_frontend_init_local_declaration_callbacks_in_contexts(
      &stream->local_declarations, semantic_context,
      compiler_context
          ? compiler_context->global_registry
          : ps_global_registry_active(),
      compiler_context
          ? compiler_context->local_registry
          : ps_local_registry_active());
  ps_parser_stream_begin_in_contexts(
      &stream->parser, semantic_context,
      compiler_context
          ? compiler_context->local_registry
          : ps_local_registry_active(),
      tk_ctx, start,
      &stream->toplevel_declarations);
}

node_t *psx_frontend_next_function(psx_frontend_stream_t *stream) {
  if (!stream) return NULL;
  psx_parsed_toplevel_item_t item;
  while (ps_parse_next_toplevel_item(&stream->parser, &item)) {
    if (item.kind == PSX_TOPLEVEL_ITEM_STATIC_ASSERT) {
      psx_apply_static_assert_in_contexts(
          stream->compiler_context
              ? stream->compiler_context->semantic_context : ps_ctx_active(),
          stream->compiler_context
              ? stream->compiler_context->local_registry
              : ps_local_registry_active(),
          item.value.static_assertion.condition,
          item.value.static_assertion.diagnostic_token);
      continue;
    }
    if (item.kind == PSX_TOPLEVEL_ITEM_DECLARATION) {
      psx_apply_toplevel_declaration_in_contexts(
          stream->compiler_context
              ? stream->compiler_context->semantic_context : ps_ctx_active(),
          stream->compiler_context
              ? stream->compiler_context->global_registry
              : ps_global_registry_active(),
          stream->compiler_context
              ? stream->compiler_context->local_registry
              : ps_local_registry_active(),
          &item.value.declaration);
      ps_dispose_toplevel_declaration_syntax(
          &item.value.declaration);
      continue;
    }
    if (item.kind == PSX_TOPLEVEL_ITEM_FUNCTION_HEADER) {
      arena_checkpoint_t arena_mark = arena_checkpoint();
      token_ident_t *function_name =
          item.value.function_header.declarator.identifier;
      psx_function_registration_checkpoint_t checkpoint;
      psx_semantic_context_t *semantic_context = stream->compiler_context
          ? stream->compiler_context->semantic_context : ps_ctx_active();
      ps_ctx_checkpoint_function_registration_in(
          semantic_context,
          function_name ? function_name->str : NULL,
          function_name ? function_name->len : 0, &checkpoint);
      node_function_definition_t *header =
          psx_apply_function_definition_header_in_contexts(
              semantic_context,
              stream->compiler_context
                  ? stream->compiler_context->global_registry
                  : ps_global_registry_active(),
              stream->compiler_context
                  ? stream->compiler_context->local_registry
                  : ps_local_registry_active(),
              &item.value.function_header);
      node_t *function = ps_parse_function_definition_body(
          &stream->parser, header,
          &stream->local_declarations);
      if (!function) {
        ps_ctx_rollback_function_registration_in(
            semantic_context,
            function_name ? function_name->str : NULL,
            function_name ? function_name->len : 0, &checkpoint);
        ps_decl_reset_locals_in(
            stream->compiler_context
                ? stream->compiler_context->local_registry
                : ps_local_registry_active());
        ps_ctx_reset_function_scope_in(semantic_context);
        ps_dispose_function_definition_header_syntax(
            &item.value.function_header);
        arena_rollback(arena_mark);
        if (agc_wasm_diagnostic_limit_kind()) return NULL;
        continue;
      }
      ps_dispose_function_definition_header_syntax(
          &item.value.function_header);
      psx_frontend_analyze_function_in_compiler_context(
          stream->compiler_context, function, function->tok);
      return function;
    }
  }
  return NULL;
}

void psx_frontend_stream_end(psx_frontend_stream_t *stream) {
  if (!stream) return;
  ps_ctx_emit_deferred_parser_warnings_in(
      stream->compiler_context
          ? stream->compiler_context->semantic_context : ps_ctx_active());
  ps_parser_stream_end(&stream->parser);
}

void psx_frontend_free_processed_ast(void) {
  arena_free_all();
}

node_t **psx_frontend_program_ctx(
    tokenizer_context_t *tk_ctx, token_t *start) {
  psx_frontend_stream_t stream = {0};
  psx_frontend_stream_begin(&stream, NULL, tk_ctx, start);
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
  psx_frontend_analyze_program_in_contexts(
      stream.toplevel_declarations.semantic_context,
      stream.toplevel_declarations.global_registry,
      stream.toplevel_declarations.local_registry,
      program);
  return program;
}

node_t **psx_frontend_program_from(token_t *start) {
  return psx_frontend_program_ctx(NULL, start);
}

node_t **psx_frontend_program(void) {
  return psx_frontend_program_ctx(NULL, tk_get_current_token());
}
