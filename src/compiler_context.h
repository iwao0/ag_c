#ifndef AG_COMPILER_CONTEXT_H
#define AG_COMPILER_CONTEXT_H

#include "compilation_session.h"

/* Compatibility name while context-free parser APIs are being removed. */
typedef ag_compilation_session_t ag_compiler_context_t;

int ag_compiler_context_init(ag_compiler_context_t *context);
int ag_compiler_context_is_complete(const ag_compiler_context_t *context);
int ag_compiler_context_activate(ag_compiler_context_t *context);
void ag_compiler_context_deactivate(ag_compiler_context_t *context);
void ag_compiler_context_dispose(ag_compiler_context_t *context);

#endif
