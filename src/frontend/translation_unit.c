#include "translation_unit.h"
#include "translation_unit_resolver.h"

#include "../diag/diag.h"
#include "../declaration_pipeline.h"
#include "../lowering/local_storage.h"
#include "../lowering/runtime_context.h"
#include "../hir/hir.h"
#include "../semantic/declaration_registration.h"
#include "local_declaration.h"
#include "semantic_pipeline.h"
#include "toplevel_declaration.h"
#include "../parser/arena.h"
#include "../parser/declaration_binding_events.h"
#include "../parser/decl.h"
#include "../parser/expr.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/runtime_context.h"
#include "../parser/semantic_ctx.h"
#include "../parser/statement_syntax_adapter.h"
#include <string.h>

static int frontend_session_is_complete(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session);
}

static node_t *parse_toplevel_assignment_expression(void *context) {
  psx_frontend_stream_t *stream = context;
  ag_compilation_session_t *session =
      stream ? stream->session : NULL;
  if (!frontend_session_is_complete(session)) return NULL;
  return psx_expr_assign_with_syntax_services(
      ag_compilation_session_parser_runtime_context(session),
      &stream->parser.syntax.name_classifier, NULL, NULL, 0);
}

static void diagnose_toplevel_unsupported_gnu_extension(
    void *context, const token_t *token, const char *name) {
  psx_frontend_stream_t *stream = context;
  ag_compilation_session_t *session =
      stream ? stream->session : NULL;
  if (!frontend_session_is_complete(session)) return;
  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(session);
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION,
      token,
      diag_message_for_in(
          diagnostics, DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION),
      name ? name : "");
}

static int parse_toplevel_static_assert_syntax(
    void *context,
    psx_parsed_static_assert_declaration_t *assertion,
    const psx_name_classifier_t *name_classifier) {
  psx_frontend_stream_t *stream = context;
  if (!stream || !assertion || !name_classifier) return 0;
  psx_parse_static_assert_syntax_with_context(
      assertion,
      &(psx_static_assert_syntax_context_t){
          .context = stream,
          .runtime_context =
              stream->parser_syntax.runtime_context,
          .parse_assignment_expression =
              parse_toplevel_assignment_expression,
      });
  return 1;
}

static int parse_toplevel_declaration_head_syntax(
    void *context,
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_name_classifier_t *name_classifier) {
  psx_frontend_stream_t *stream = context;
  ag_compilation_session_t *session =
      stream ? stream->session : NULL;
  if (!frontend_session_is_complete(session)) return 0;
  psx_toplevel_declaration_syntax_context_t syntax = {
      .context = stream,
      .name_classifier = *name_classifier,
      .runtime_context =
          ag_compilation_session_parser_runtime_context(session),
      .parse_assignment_expression =
          parse_toplevel_assignment_expression,
      .diagnose_unsupported_gnu_extension =
          diagnose_toplevel_unsupported_gnu_extension,
  };
  return psx_parse_toplevel_declaration_head_syntax_with_context(
      declaration, &syntax);
}

static int finish_toplevel_declaration_syntax(
    void *context,
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_name_classifier_t *name_classifier) {
  psx_frontend_stream_t *stream = context;
  ag_compilation_session_t *session =
      stream ? stream->session : NULL;
  if (!frontend_session_is_complete(session)) return 0;
  psx_toplevel_declaration_syntax_context_t syntax = {
      .context = stream,
      .name_classifier = *name_classifier,
      .runtime_context =
          ag_compilation_session_parser_runtime_context(session),
      .parse_assignment_expression =
          parse_toplevel_assignment_expression,
      .diagnose_unsupported_gnu_extension =
          diagnose_toplevel_unsupported_gnu_extension,
  };
  return psx_finish_toplevel_declaration_syntax_with_context(
      declaration, &syntax);
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
  psx_hir_module_reset(
      ag_compilation_session_hir_module(session));
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
  psx_parser_runtime_context_t *runtime_context =
      ag_compilation_session_parser_runtime_context(session);
  ps_global_registry_reset_diag_state_in(global_registry);
  ps_ctx_reset_function_diag_state_in(semantic_context);
  ps_ctx_reset_tag_diag_state_in(semantic_context);
  ps_ctx_reset_function_names_in(semantic_context);
  psx_name_classifier_t empty_classifier = {0};
  psx_frontend_init_local_declaration_syntax_adapter(
      &stream->local_declaration_adapter,
      &stream->local_declarations, runtime_context,
      &empty_classifier, NULL, 0);
  ps_parser_name_environment_init(
      &stream->local_name_environment,
      empty_classifier);
  stream->local_declarations.name_classifier =
      ps_parser_name_environment_classifier(
          &stream->local_name_environment);
  stream->parser_syntax = (psx_parser_syntax_services_t){
      .context = stream,
      .runtime_context = runtime_context,
      .name_classifier = empty_classifier,
      .parse_static_assert =
          parse_toplevel_static_assert_syntax,
      .parse_toplevel_declaration_head =
          parse_toplevel_declaration_head_syntax,
      .finish_toplevel_declaration =
          finish_toplevel_declaration_syntax,
  };
  ps_parser_stream_begin_with_syntax(
      &stream->parser, tk_ctx, start, &stream->parser_syntax);
  stream->is_started = 1;
  return 1;
}

int psx_frontend_next_function_with_resolver(
    psx_frontend_stream_t *stream, psx_frontend_function_t *result,
    psx_frontend_function_resolver_t resolver,
    void *resolver_context) {
  if (result) result->hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!stream || !stream->is_started ||
      !frontend_session_is_complete(stream->session) ||
      !ag_compilation_session_is_active(stream->session) ||
      !result || !resolver) {
    return 0;
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
          ag_compilation_session_lowering_context(stream->session),
          ag_compilation_session_options_view(stream->session),
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
      ps_decl_reset_locals_in(local_registry);
      local_storage_reset(
          ag_compilation_session_lowering_context(session));
      ps_ctx_reset_function_scope_in(semantic_context);
      ps_decl_set_current_funcname_in(
          local_registry,
          function_name ? function_name->str : NULL,
          function_name ? function_name->len : 0);
      ps_parser_name_environment_reset_at(
          &stream->local_name_environment,
          stream->parser.syntax.name_classifier,
          0, 0, 0);
      stream->local_declarations.name_classifier =
          ps_parser_name_environment_classifier(
              &stream->local_name_environment);
      psx_record_function_definition_declarator_binding_events(
          &item.value.function_header.declarator,
          &stream->local_declarations.name_classifier);
      psx_frontend_local_declaration_syntax_set_function_name(
          &stream->local_declaration_adapter,
          function_name ? function_name->str : NULL,
          function_name ? function_name->len : 0);
      psx_statement_syntax_adapter_t statement_adapter;
      if (!psx_statement_syntax_adapter_init(
              &statement_adapter,
              ag_compilation_session_parser_runtime_context(session),
              &stream->local_declarations.name_classifier,
              &stream->local_declarations,
              function_name ? function_name->str : NULL,
              function_name ? function_name->len : 0))
        return 0;
      psx_statement_syntax_context_t statement_syntax =
          psx_statement_syntax_adapter_context(
              &statement_adapter);
      if (!ps_parse_function_definition_body(
              &stream->parser, &item.value.function_header,
              &statement_syntax)) {
        ps_decl_reset_locals_in(local_registry);
        ps_decl_set_current_funcname_in(
            local_registry, NULL, 0);
        local_storage_reset(
            ag_compilation_session_lowering_context(session));
        ps_ctx_reset_function_scope_in(semantic_context);
        ps_dispose_function_definition_syntax(
            &item.value.function_header);
        arena_rollback_in(
            ag_compilation_session_arena_context(session), arena_mark);
        if (diag_limit_kind_in(
                ag_compilation_session_diagnostic_context(session)))
          return 0;
        continue;
      }
      ps_decl_set_current_funcname_in(local_registry, NULL, 0);
      psx_function_registration_checkpoint_t checkpoint;
      ps_ctx_checkpoint_function_registration_in(
          semantic_context,
          function_name ? function_name->str : NULL,
          function_name ? function_name->len : 0, &checkpoint);
      if (!resolver(
              resolver_context, session,
              &item.value.function_header,
              (token_t *)function_name, &result->hir_root)) {
        ps_ctx_rollback_function_registration_in(
            semantic_context,
            function_name ? function_name->str : NULL,
            function_name ? function_name->len : 0, &checkpoint);
        ps_dispose_function_definition_syntax(
            &item.value.function_header);
        arena_rollback_in(
            ag_compilation_session_arena_context(session), arena_mark);
        return 0;
      }
      ps_dispose_function_definition_syntax(
          &item.value.function_header);
      return 1;
    }
  }
  return 0;
}

static int resolve_function_to_hir(
    void *context, ag_compilation_session_t *session,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  (void)context;
  return psx_frontend_resolve_parsed_function_to_hir_in_session(
      session, syntax_function, fallback_diag_tok, hir_root);
}

int psx_frontend_next_function(
    psx_frontend_stream_t *stream, psx_frontend_function_t *result) {
  return psx_frontend_next_function_with_resolver(
      stream, result, resolve_function_to_hir, NULL);
}

int psx_frontend_stream_end(psx_frontend_stream_t *stream) {
  if (!stream || !stream->is_started ||
      !frontend_session_is_complete(stream->session) ||
      !ag_compilation_session_is_active(stream->session))
    return 0;
  ps_ctx_emit_deferred_parser_diagnostics_in(
      ag_compilation_session_semantic_context(stream->session));
  ps_parser_stream_end(&stream->parser);
  ps_parser_name_environment_dispose(
      &stream->local_name_environment);
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
