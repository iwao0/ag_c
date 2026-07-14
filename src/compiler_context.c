#include "compiler_context.h"

#include "parser/semantic_ctx.h"
#include <string.h>

int ag_compiler_context_init(ag_compiler_context_t *context) {
  if (!context) return 0;
  memset(context, 0, sizeof(*context));
  context->semantic_context = ps_ctx_create();
  return context->semantic_context != NULL;
}

int ag_compiler_context_activate(ag_compiler_context_t *context) {
  if (!context || !context->semantic_context || context->is_active) return 0;
  context->previous_semantic_context =
      ps_ctx_activate(context->semantic_context);
  context->is_active = 1;
  return 1;
}

void ag_compiler_context_deactivate(ag_compiler_context_t *context) {
  if (!context || !context->is_active) return;
  ps_ctx_activate(context->previous_semantic_context);
  context->previous_semantic_context = NULL;
  context->is_active = 0;
}

void ag_compiler_context_dispose(ag_compiler_context_t *context) {
  if (!context) return;
  ag_compiler_context_deactivate(context);
  ps_ctx_destroy(context->semantic_context);
  memset(context, 0, sizeof(*context));
}
