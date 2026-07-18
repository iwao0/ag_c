#include "compilation_session_internal.h"

#include "parser/global_registry.h"
#include "parser/local_registry.h"
#include "parser/semantic_ctx.h"
#include "parser/arena.h"
#include "parser/runtime_context.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include "tokenizer/allocator.h"
#include "lowering/runtime_context.h"
#include "hir/hir.h"
#include "semantic/scope_graph.h"
#include "codegen_emit.h"
#include <string.h>
#include <stdlib.h>

static ag_compilation_session_t *active_compilation_session;

static char *session_strdup(const char *text) {
  if (!text || !text[0]) return NULL;
  size_t len = strlen(text);
  char *copy = malloc(len + 1);
  if (copy) memcpy(copy, text, len + 1);
  return copy;
}

static void dispose_continuation_options(ag_continuation_options_t *options) {
  if (!options) return;
  free(options->entry);
  free(options->frame_condition);
  free(options->start_export);
  free(options->resume_export);
  free(options->status_export);
  free(options->result_export);
  memset(options, 0, sizeof(*options));
}

ag_compilation_session_t *ag_compilation_session_create(
    const ag_target_info_t *target) {
  ag_compilation_session_t *session = malloc(sizeof(*session));
  if (!session) return NULL;
  if (!ag_compilation_session_init(session, target)) {
    free(session);
    return NULL;
  }
  return session;
}

int ag_compilation_session_init(
    ag_compilation_session_t *session, const ag_target_info_t *target) {
  if (!session) return 0;
  memset(session, 0, sizeof(*session));
  ag_compilation_options_init_defaults(&session->options);
  tk_context_init(&session->tokenizer);
  session->target = target ? *target : ag_target_info_host();
  session->target.pointer_size =
      ag_target_info_pointer_size(&session->target);
  session->diagnostic_context = diag_context_create();
  diag_context_bind_tokenizer(
      session->diagnostic_context, &session->tokenizer);
  tk_context_bind_diagnostic_context(
      &session->tokenizer, session->diagnostic_context);
  session->arena_context = arena_context_create();
  session->scope_graph = psx_scope_graph_create();
  session->semantic_context = ps_ctx_create(session->arena_context);
  session->hir_module = psx_hir_module_create();
  session->global_registry = ps_global_registry_create();
  ps_global_registry_bind_semantic_types(
      session->global_registry,
      ps_ctx_semantic_type_table_in(session->semantic_context));
  ps_global_registry_bind_scope_graph(
      session->global_registry, session->scope_graph);
  session->local_registry = ps_local_registry_create(
      session->diagnostic_context);
  ps_local_registry_bind_semantic_types(
      session->local_registry,
      ps_ctx_semantic_type_table_in(session->semantic_context));
  ps_local_registry_bind_scope_graph(
      session->local_registry, session->scope_graph);
  ps_ctx_bind_scope_graph(session->semantic_context, session->scope_graph);
  ps_ctx_bind_diagnostic_context(
      session->semantic_context, session->diagnostic_context);
  ps_ctx_bind_target_info(session->semantic_context, &session->target);
  session->preprocessor_context = pp_context_create(
      session->diagnostic_context);
  session->token_allocator_context = tk_allocator_context_create(
      session->diagnostic_context);
  tk_context_set_allocator(
      &session->tokenizer, session->token_allocator_context);
  session->parser_runtime_context = ps_parser_runtime_context_create(
      session->arena_context, &session->tokenizer,
      session->diagnostic_context);
  session->lowering_context = ps_lowering_context_create(
      session->arena_context, session->diagnostic_context);
  ps_lowering_context_bind_target(session->lowering_context, &session->target);
  ps_lowering_context_bind_semantic_types(
      session->lowering_context,
      ps_ctx_semantic_type_table_in(session->semantic_context));
  ps_lowering_context_bind_record_decls(
      session->lowering_context,
      ps_ctx_record_decl_table_in(session->semantic_context));
  ps_lowering_context_bind_record_layouts(
      session->lowering_context,
      ps_ctx_record_layout_table_in(session->semantic_context));
  session->codegen_emit_context = cg_context_create(
      session->diagnostic_context);
  if (!session->scope_graph || !session->semantic_context ||
      !session->global_registry ||
      !session->local_registry || !session->preprocessor_context ||
      !session->arena_context || !session->diagnostic_context ||
      !session->token_allocator_context || !session->parser_runtime_context ||
      !session->lowering_context || !session->hir_module ||
      !session->codegen_emit_context) {
    psx_hir_module_destroy(session->hir_module);
    psx_scope_graph_destroy(session->scope_graph);
    ps_ctx_destroy(session->semantic_context);
    ps_global_registry_destroy(session->global_registry);
    ps_local_registry_destroy(session->local_registry);
    pp_context_destroy(session->preprocessor_context);
    diag_context_destroy(session->diagnostic_context);
    tk_allocator_context_destroy(session->token_allocator_context);
    ps_parser_runtime_context_destroy(session->parser_runtime_context);
    ps_lowering_context_destroy(session->lowering_context);
    cg_context_destroy(session->codegen_emit_context);
    arena_context_destroy(session->arena_context);
    memset(session, 0, sizeof(*session));
    return 0;
  }
  return 1;
}

int ag_compilation_session_is_complete(
    const ag_compilation_session_t *session) {
  return session && session->scope_graph && session->semantic_context &&
         session->global_registry &&
         session->local_registry && session->preprocessor_context &&
         session->arena_context &&
         session->diagnostic_context && session->token_allocator_context &&
         session->parser_runtime_context &&
         session->lowering_context &&
         session->hir_module &&
         ps_lowering_semantic_types(session->lowering_context) &&
         ps_lowering_record_decls(session->lowering_context) &&
         ps_lowering_record_layouts(session->lowering_context) &&
         session->codegen_emit_context &&
         (session->target.pointer_size == 4 ||
          session->target.pointer_size == 8);
}

int ag_compilation_session_activate(ag_compilation_session_t *session) {
  if (!ag_compilation_session_is_complete(session) || session->is_active)
    return 0;
  session->previous_session = active_compilation_session;
  active_compilation_session = session;
  if (session->backend_activate)
    session->backend_activate(session->backend_context);
  session->is_active = 1;
  return 1;
}

int ag_compilation_session_is_active(
    const ag_compilation_session_t *session) {
  return session && session->is_active &&
         active_compilation_session == session;
}

int ag_compilation_session_deactivate(ag_compilation_session_t *session) {
  if (!session || !session->is_active ||
      active_compilation_session != session)
    return 0;
  if (session->backend_deactivate)
    session->backend_deactivate(session->backend_context);
  active_compilation_session = session->previous_session;
  session->previous_session = NULL;
  session->is_active = 0;
  return 1;
}

int ag_compilation_session_dispose(ag_compilation_session_t *session) {
  if (!session) return 0;
  if (session->is_active && !ag_compilation_session_deactivate(session))
    return 0;
  psx_hir_module_destroy(session->hir_module);
  ps_ctx_destroy(session->semantic_context);
  ps_global_registry_destroy(session->global_registry);
  ps_local_registry_destroy(session->local_registry);
  psx_scope_graph_destroy(session->scope_graph);
  pp_context_destroy(session->preprocessor_context);
  diag_context_destroy(session->diagnostic_context);
  tk_allocator_context_destroy(session->token_allocator_context);
  ps_parser_runtime_context_destroy(session->parser_runtime_context);
  ps_lowering_context_destroy(session->lowering_context);
  tk_context_dispose(&session->tokenizer);
  cg_context_destroy(session->codegen_emit_context);
  dispose_continuation_options(&session->continuation);
  if (session->backend_destroy)
    session->backend_destroy(session->backend_context);
  arena_context_destroy(session->arena_context);
  memset(session, 0, sizeof(*session));
  return 1;
}

int ag_compilation_session_destroy(ag_compilation_session_t *session) {
  if (!session || !ag_compilation_session_dispose(session)) return 0;
  free(session);
  return 1;
}

tokenizer_context_t *ag_compilation_session_tokenizer(
    ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? &session->tokenizer
             : NULL;
}

tk_allocator_context_t *ag_compilation_session_token_allocator_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->token_allocator_context : NULL;
}

psx_semantic_context_t *ag_compilation_session_semantic_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->semantic_context
             : NULL;
}

psx_global_registry_t *ag_compilation_session_global_registry(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->global_registry
             : NULL;
}

psx_local_registry_t *ag_compilation_session_local_registry(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->local_registry
             : NULL;
}

ag_preprocessor_context_t *ag_compilation_session_preprocessor_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->preprocessor_context
             : NULL;
}

arena_context_t *ag_compilation_session_arena_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->arena_context
             : NULL;
}

ag_diagnostic_context_t *ag_compilation_session_diagnostic_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->diagnostic_context
             : NULL;
}

ag_codegen_emit_context_t *ag_compilation_session_codegen_emit_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->codegen_emit_context
             : NULL;
}

psx_parser_runtime_context_t *ag_compilation_session_parser_runtime_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->parser_runtime_context
             : NULL;
}

psx_lowering_context_t *ag_compilation_session_lowering_context(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->lowering_context
             : NULL;
}

psx_hir_module_t *ag_compilation_session_hir_module(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->hir_module : NULL;
}

psx_scope_graph_t *ag_compilation_session_scope_graph(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? session->scope_graph : NULL;
}

ag_compilation_options_t *ag_compilation_session_options(
    ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? &session->options
             : NULL;
}

const ag_compilation_options_t *ag_compilation_session_options_view(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? &session->options
             : NULL;
}

const ag_target_info_t *ag_compilation_session_target(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? &session->target
             : NULL;
}

int ag_compilation_session_set_backend_context(
    ag_compilation_session_t *session, void *backend_context,
    ag_session_backend_callback_t activate,
    ag_session_backend_callback_t deactivate,
    ag_session_backend_callback_t destroy) {
  if (!session || session->is_active || session->backend_context ||
      !backend_context || !destroy)
    return 0;
  session->backend_context = backend_context;
  session->backend_activate = activate;
  session->backend_deactivate = deactivate;
  session->backend_destroy = destroy;
  return 1;
}

int ag_compilation_session_set_continuation(
    ag_compilation_session_t *session, const char *entry,
    const char *frame_condition, const char *start_export,
    const char *resume_export, const char *status_export,
    const char *result_export) {
  if (!session || session->is_active || !entry || !entry[0] ||
      !frame_condition || !frame_condition[0])
    return 0;
  ag_continuation_options_t next = {0};
  next.entry = session_strdup(entry);
  next.frame_condition = session_strdup(frame_condition);
  next.start_export = session_strdup(
      start_export && start_export[0] ? start_export : entry);
  next.resume_export = session_strdup(
      resume_export && resume_export[0]
          ? resume_export : "__agc_continuation_resume");
  next.status_export = session_strdup(
      status_export && status_export[0]
          ? status_export : "__agc_continuation_status");
  next.result_export = session_strdup(
      result_export && result_export[0]
          ? result_export : "__agc_continuation_result");
  if (!next.entry || !next.frame_condition || !next.start_export ||
      !next.resume_export || !next.status_export || !next.result_export) {
    dispose_continuation_options(&next);
    return 0;
  }
  next.enabled = 1;
  dispose_continuation_options(&session->continuation);
  session->continuation = next;
  return 1;
}

const ag_continuation_options_t *ag_compilation_session_continuation(
    const ag_compilation_session_t *session) {
  return session && session->continuation.enabled
             ? &session->continuation : NULL;
}
