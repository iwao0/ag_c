#ifndef AG_COMPILATION_SESSION_H
#define AG_COMPILATION_SESSION_H

#include "target_info.h"
#include "tokenizer/tokenizer.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct ag_preprocessor_context_t ag_preprocessor_context_t;
typedef struct arena_context_t arena_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct tk_allocator_context_t tk_allocator_context_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct ag_compilation_session_t {
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
  ag_target_info_t target;
  unsigned char is_active;
} ag_compilation_session_t;

int ag_compilation_session_init(
    ag_compilation_session_t *session, const ag_target_info_t *target);
int ag_compilation_session_is_complete(
    const ag_compilation_session_t *session);
int ag_compilation_session_activate(ag_compilation_session_t *session);
void ag_compilation_session_deactivate(ag_compilation_session_t *session);
void ag_compilation_session_dispose(ag_compilation_session_t *session);
tokenizer_context_t *ag_compilation_session_tokenizer(
    ag_compilation_session_t *session);
const ag_target_info_t *ag_compilation_session_target(
    const ag_compilation_session_t *session);

#endif
