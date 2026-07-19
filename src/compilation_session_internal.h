#ifndef AG_COMPILATION_SESSION_INTERNAL_H
#define AG_COMPILATION_SESSION_INTERNAL_H

#include "compilation_session.h"
#include "semantic/resolution_store.h"
#include "tokenizer/tokenizer.h"

int ag_compilation_session_init(
    ag_compilation_session_t *session, const ag_target_info_t *target);
int ag_compilation_session_dispose(ag_compilation_session_t *session);

struct ag_compilation_session_t {
  psx_semantic_context_t *semantic_context;
  psx_resolution_store_t *resolution_store;
  psx_scope_graph_t *scope_graph;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  ag_preprocessor_context_t *preprocessor_context;
  arena_context_t *arena_context;
  ag_diagnostic_context_t *diagnostic_context;
  tokenizer_context_t tokenizer;
  tk_allocator_context_t *token_allocator_context;
  psx_parser_runtime_context_t *parser_runtime_context;
  psx_lowering_context_t *lowering_context;
  psx_hir_module_t *hir_module;
  ag_codegen_emit_context_t *codegen_emit_context;
  ag_compilation_options_t options;
  ag_continuation_options_t continuation;
  void *backend_context;
  ag_session_backend_destroy_fn backend_destroy;
  ag_target_info_t target;
};

#endif
