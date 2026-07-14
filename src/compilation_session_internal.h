#ifndef AG_COMPILATION_SESSION_INTERNAL_H
#define AG_COMPILATION_SESSION_INTERNAL_H

#include "compilation_session.h"

struct ag_compilation_session_t {
  struct ag_compilation_session_t *previous_session;
  psx_semantic_context_t *semantic_context;
  psx_semantic_context_t *previous_semantic_context;
  psx_global_registry_t *global_registry;
  psx_global_registry_t *previous_global_registry;
  psx_local_registry_t *local_registry;
  psx_local_registry_t *previous_local_registry;
  ag_preprocessor_context_t *preprocessor_context;
  ag_preprocessor_context_t *previous_preprocessor_context;
  arena_context_t *arena_context;
  arena_context_t *previous_arena_context;
  ag_diagnostic_context_t *diagnostic_context;
  ag_diagnostic_context_t *previous_diagnostic_context;
  tokenizer_context_t tokenizer;
  tokenizer_context_t *previous_tokenizer_context;
  tk_allocator_context_t *token_allocator_context;
  tk_allocator_context_t *previous_token_allocator_context;
  psx_parser_runtime_context_t *parser_runtime_context;
  psx_parser_runtime_context_t *previous_parser_runtime_context;
  psx_lowering_context_t *lowering_context;
  psx_lowering_context_t *previous_lowering_context;
  ag_codegen_emit_context_t *codegen_emit_context;
  ag_codegen_emit_context_t *previous_codegen_emit_context;
  ag_continuation_options_t continuation;
  void *backend_context;
  ag_session_backend_callback_t backend_activate;
  ag_session_backend_callback_t backend_deactivate;
  ag_session_backend_callback_t backend_destroy;
  ag_target_info_t target;
  unsigned char is_active;
};

#endif
