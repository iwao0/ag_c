#include "translation_unit.h"

#include "../diag/diag.h"
#include "../declaration_pipeline.h"
#include "../lowering/runtime_context.h"
#include "../semantic/declaration_registration.h"
#include "function_definition.h"
#include "local_declaration.h"
#include "semantic_pipeline.h"
#include "toplevel_declaration.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/runtime_context.h"
#include "../parser/semantic_ctx.h"
#include <stdlib.h>
#include <string.h>

static int frontend_session_is_complete(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session);
}

static void reset_translation_unit_state(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    arena_context_t *arena_context) {
  ps_global_registry_reset_translation_unit_in(global_registry);
  ps_decl_reset_translation_unit_state_in(local_registry);
  ps_ctx_reset_translation_unit_scope_in(semantic_context);
  ps_parser_runtime_context_reset_translation_unit(runtime_context);
  psx_declaration_pipeline_reset_translation_unit_state_in(
      lowering_context);
  arena_free_all_in(arena_context);
}

int psx_frontend_reset_translation_unit_state_in_session(
    ag_compilation_session_t *session) {
  if (!frontend_session_is_complete(session)) return 0;
  reset_translation_unit_state(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_parser_runtime_context(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_arena_context(session));
  return 1;
}

int psx_frontend_stream_begin(
    psx_frontend_stream_t *stream,
    ag_compilation_session_t *session,
    tokenizer_context_t *tk_ctx, token_t *start) {
  if (!stream) return 0;
  memset(stream, 0, sizeof(*stream));
  if (!frontend_session_is_complete(session)) return 0;
  if (!ag_compilation_session_is_active(session)) {
    if (!ag_compilation_session_activate(session)) return 0;
    stream->owns_session_activation = 1;
  }
  stream->session = session;
  psx_semantic_context_t *semantic_context =
      ag_compilation_session_semantic_context(session);
  psx_global_registry_t *global_registry =
      ag_compilation_session_global_registry(session);
  psx_local_registry_t *local_registry =
      ag_compilation_session_local_registry(session);
  psx_parser_runtime_context_t *runtime_context =
      ag_compilation_session_parser_runtime_context(session);
  ps_global_registry_reset_diag_state_in(global_registry);
  ps_ctx_reset_function_diag_state_in(semantic_context);
  ps_ctx_reset_tag_diag_state_in(semantic_context);
  ps_ctx_reset_function_names_in(semantic_context);
  psx_frontend_init_toplevel_declaration_callbacks_in_contexts(
      &stream->toplevel_declarations, semantic_context,
      global_registry, local_registry, runtime_context);
  psx_frontend_init_local_declaration_callbacks_in_contexts(
      &stream->local_declarations, semantic_context,
      global_registry, local_registry, runtime_context);
  ps_parser_stream_begin_in_contexts(
      &stream->parser, semantic_context, global_registry, local_registry,
      runtime_context,
      tk_ctx, start,
      &stream->toplevel_declarations);
  stream->is_started = 1;
  return 1;
}

node_t *psx_frontend_next_function(psx_frontend_stream_t *stream) {
  if (!stream || !stream->is_started ||
      !frontend_session_is_complete(stream->session) ||
      !ag_compilation_session_is_active(stream->session)) {
    return NULL;
  }
  ag_compilation_session_t *session = stream->session;
  psx_semantic_context_t *semantic_context =
      ag_compilation_session_semantic_context(session);
  psx_global_registry_t *global_registry =
      ag_compilation_session_global_registry(session);
  psx_local_registry_t *local_registry =
      ag_compilation_session_local_registry(session);
  psx_parsed_toplevel_item_t item;
  while (ps_parse_next_toplevel_item(&stream->parser, &item)) {
    if (item.kind == PSX_TOPLEVEL_ITEM_STATIC_ASSERT) {
      psx_apply_static_assert_in_contexts(
          semantic_context, global_registry, local_registry,
          item.value.static_assertion.condition,
          item.value.static_assertion.diagnostic_token);
      continue;
    }
    if (item.kind == PSX_TOPLEVEL_ITEM_DECLARATION) {
      psx_apply_toplevel_declaration_in_contexts(
          semantic_context, global_registry, local_registry,
          ag_compilation_session_parser_runtime_context(stream->session),
          &item.value.declaration);
      ps_dispose_toplevel_declaration_syntax(
          &item.value.declaration);
      continue;
    }
    if (item.kind == PSX_TOPLEVEL_ITEM_FUNCTION_HEADER) {
      arena_checkpoint_t arena_mark =
          arena_checkpoint_in(
              ag_compilation_session_arena_context(session));
      token_ident_t *function_name =
          item.value.function_header.declarator.identifier;
      psx_function_registration_checkpoint_t checkpoint;
      ps_ctx_checkpoint_function_registration_in(
          semantic_context,
          function_name ? function_name->str : NULL,
          function_name ? function_name->len : 0, &checkpoint);
      node_function_definition_t *header =
          psx_apply_function_definition_header_in_contexts(
              semantic_context, global_registry, local_registry,
              ag_compilation_session_parser_runtime_context(session),
              &item.value.function_header);
      node_t *function = ps_parse_function_definition_body(
          &stream->parser, header,
          &stream->local_declarations);
      if (!function) {
        ps_ctx_rollback_function_registration_in(
            semantic_context,
            function_name ? function_name->str : NULL,
            function_name ? function_name->len : 0, &checkpoint);
        ps_decl_reset_locals_in(local_registry);
        ps_ctx_reset_function_scope_in(semantic_context);
        ps_dispose_function_definition_header_syntax(
            &item.value.function_header);
        arena_rollback_in(
            ag_compilation_session_arena_context(session), arena_mark);
        if (diag_active_limit_kind()) return NULL;
        continue;
      }
      ps_dispose_function_definition_header_syntax(
          &item.value.function_header);
      psx_frontend_analyze_function_in_session(
          session, function, function->tok);
      return function;
    }
  }
  return NULL;
}

int psx_frontend_stream_end(psx_frontend_stream_t *stream) {
  if (!stream || !stream->is_started ||
      !frontend_session_is_complete(stream->session) ||
      !ag_compilation_session_is_active(stream->session))
    return 0;
  ps_ctx_emit_deferred_parser_warnings_in(
      ag_compilation_session_semantic_context(stream->session));
  ps_parser_stream_end(&stream->parser);
  stream->is_started = 0;
  if (stream->owns_session_activation) {
    if (!ag_compilation_session_deactivate(stream->session)) return 0;
    stream->owns_session_activation = 0;
  }
  return 1;
}

int psx_frontend_free_processed_ast_in_session(
    ag_compilation_session_t *session) {
  if (!frontend_session_is_complete(session)) return 0;
  arena_free_all_in(ag_compilation_session_arena_context(session));
  return 1;
}

node_t **psx_frontend_program_in_session(
    ag_compilation_session_t *session,
    tokenizer_context_t *tk_ctx, token_t *start) {
  psx_frontend_stream_t stream = {0};
  if (!psx_frontend_stream_begin(
          &stream, session, tk_ctx, start)) {
    return NULL;
  }
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
  psx_frontend_analyze_program_in_session(session, program);
  if (!psx_frontend_stream_end(&stream)) {
    free(program);
    return NULL;
  }
  return program;
}
