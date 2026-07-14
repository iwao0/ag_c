#ifndef AG_COMPILATION_SESSION_H
#define AG_COMPILATION_SESSION_H

#include "continuation_options.h"
#include "target_info.h"

typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct ag_preprocessor_context_t ag_preprocessor_context_t;
typedef struct arena_context_t arena_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct tk_allocator_context_t tk_allocator_context_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;
typedef struct ag_compilation_session_t ag_compilation_session_t;
typedef void (*ag_session_backend_callback_t)(void *context);

ag_compilation_session_t *ag_compilation_session_create(
    const ag_target_info_t *target);
int ag_compilation_session_is_complete(
    const ag_compilation_session_t *session);
int ag_compilation_session_activate(ag_compilation_session_t *session);
int ag_compilation_session_is_active(
    const ag_compilation_session_t *session);
int ag_compilation_session_deactivate(ag_compilation_session_t *session);
int ag_compilation_session_destroy(ag_compilation_session_t *session);
tokenizer_context_t *ag_compilation_session_tokenizer(
    ag_compilation_session_t *session);
psx_semantic_context_t *ag_compilation_session_semantic_context(
    const ag_compilation_session_t *session);
psx_global_registry_t *ag_compilation_session_global_registry(
    const ag_compilation_session_t *session);
psx_local_registry_t *ag_compilation_session_local_registry(
    const ag_compilation_session_t *session);
arena_context_t *ag_compilation_session_arena_context(
    const ag_compilation_session_t *session);
ag_diagnostic_context_t *ag_compilation_session_diagnostic_context(
    const ag_compilation_session_t *session);
psx_parser_runtime_context_t *ag_compilation_session_parser_runtime_context(
    const ag_compilation_session_t *session);
psx_lowering_context_t *ag_compilation_session_lowering_context(
    const ag_compilation_session_t *session);
const ag_target_info_t *ag_compilation_session_target(
    const ag_compilation_session_t *session);
int ag_compilation_session_set_backend_context(
    ag_compilation_session_t *session, void *backend_context,
    ag_session_backend_callback_t activate,
    ag_session_backend_callback_t deactivate,
    ag_session_backend_callback_t destroy);
int ag_compilation_session_set_continuation(
    ag_compilation_session_t *session, const char *entry,
    const char *frame_condition, const char *start_export,
    const char *resume_export, const char *status_export,
    const char *result_export);
const ag_continuation_options_t *ag_compilation_session_continuation(
    const ag_compilation_session_t *session);

#endif
