#include "compiler_context.h"

#include "parser/global_registry.h"
#include "parser/local_registry.h"
#include "parser/semantic_ctx.h"
#include "parser/arena.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include "tokenizer/allocator.h"
#include <string.h>

int ag_compilation_session_init(
    ag_compilation_session_t *session, const ag_target_info_t *target) {
  if (!session) return 0;
  memset(session, 0, sizeof(*session));
  tk_context_init(&session->tokenizer);
  session->target = target ? *target : ag_target_info_host();
  session->target.pointer_size =
      ag_target_info_pointer_size(&session->target);
  session->semantic_context = ps_ctx_create();
  session->global_registry = ps_global_registry_create();
  session->local_registry = ps_local_registry_create();
  session->preprocessor_context = pp_context_create();
  session->arena_context = arena_context_create();
  session->diagnostic_context = diag_context_create();
  session->token_allocator_context = tk_allocator_context_create();
  if (!session->semantic_context || !session->global_registry ||
      !session->local_registry || !session->preprocessor_context ||
      !session->arena_context || !session->diagnostic_context ||
      !session->token_allocator_context) {
    ps_ctx_destroy(session->semantic_context);
    ps_global_registry_destroy(session->global_registry);
    ps_local_registry_destroy(session->local_registry);
    pp_context_destroy(session->preprocessor_context);
    arena_context_destroy(session->arena_context);
    diag_context_destroy(session->diagnostic_context);
    tk_allocator_context_destroy(session->token_allocator_context);
    memset(session, 0, sizeof(*session));
    return 0;
  }
  return 1;
}

int ag_compilation_session_is_complete(
    const ag_compilation_session_t *session) {
  return session && session->semantic_context && session->global_registry &&
         session->local_registry && session->preprocessor_context &&
         session->arena_context &&
         session->diagnostic_context && session->token_allocator_context &&
         (session->target.pointer_size == 4 ||
          session->target.pointer_size == 8);
}

int ag_compilation_session_activate(ag_compilation_session_t *session) {
  if (!ag_compilation_session_is_complete(session) || session->is_active)
    return 0;
  session->previous_semantic_context =
      ps_ctx_activate(session->semantic_context);
  session->previous_global_registry =
      ps_global_registry_activate(session->global_registry);
  session->previous_local_registry =
      ps_local_registry_activate(session->local_registry);
  session->previous_preprocessor_context =
      pp_context_activate(session->preprocessor_context);
  session->previous_arena_context =
      arena_context_activate(session->arena_context);
  session->previous_diagnostic_context =
      diag_context_activate(session->diagnostic_context);
  session->previous_tokenizer_context =
      tk_context_activate(&session->tokenizer);
  session->previous_token_allocator_context =
      tk_allocator_context_activate(session->token_allocator_context);
  session->is_active = 1;
  return 1;
}

void ag_compilation_session_deactivate(ag_compilation_session_t *session) {
  if (!session || !session->is_active) return;
  tk_allocator_context_activate(session->previous_token_allocator_context);
  tk_context_activate(session->previous_tokenizer_context);
  diag_context_activate(session->previous_diagnostic_context);
  arena_context_activate(session->previous_arena_context);
  pp_context_activate(session->previous_preprocessor_context);
  ps_local_registry_activate(session->previous_local_registry);
  ps_global_registry_activate(session->previous_global_registry);
  ps_ctx_activate(session->previous_semantic_context);
  session->previous_local_registry = NULL;
  session->previous_preprocessor_context = NULL;
  session->previous_arena_context = NULL;
  session->previous_diagnostic_context = NULL;
  session->previous_tokenizer_context = NULL;
  session->previous_token_allocator_context = NULL;
  session->previous_global_registry = NULL;
  session->previous_semantic_context = NULL;
  session->is_active = 0;
}

void ag_compilation_session_dispose(ag_compilation_session_t *session) {
  if (!session) return;
  ag_compilation_session_deactivate(session);
  ps_ctx_destroy(session->semantic_context);
  ps_global_registry_destroy(session->global_registry);
  ps_local_registry_destroy(session->local_registry);
  pp_context_destroy(session->preprocessor_context);
  arena_context_destroy(session->arena_context);
  diag_context_destroy(session->diagnostic_context);
  tk_allocator_context_destroy(session->token_allocator_context);
  memset(session, 0, sizeof(*session));
}

tokenizer_context_t *ag_compilation_session_tokenizer(
    ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? &session->tokenizer
             : NULL;
}

const ag_target_info_t *ag_compilation_session_target(
    const ag_compilation_session_t *session) {
  return ag_compilation_session_is_complete(session)
             ? &session->target
             : NULL;
}

int ag_compiler_context_init(ag_compiler_context_t *context) {
  ag_target_info_t target = {ag_target_pointer_size()};
  return ag_compilation_session_init(context, &target);
}

int ag_compiler_context_is_complete(const ag_compiler_context_t *context) {
  return context && context->semantic_context && context->global_registry &&
         context->local_registry;
}

int ag_compiler_context_activate(ag_compiler_context_t *context) {
  return ag_compilation_session_activate(context);
}

void ag_compiler_context_deactivate(ag_compiler_context_t *context) {
  ag_compilation_session_deactivate(context);
}

void ag_compiler_context_dispose(ag_compiler_context_t *context) {
  ag_compilation_session_dispose(context);
}
