#include "compiler_context.h"

#include "parser/global_registry.h"
#include "parser/semantic_ctx.h"
#include <string.h>

int ag_compiler_context_init(ag_compiler_context_t *context) {
  if (!context) return 0;
  memset(context, 0, sizeof(*context));
  context->semantic_context = ps_ctx_create();
  context->global_registry = ps_global_registry_create();
  if (!context->semantic_context || !context->global_registry) {
    ps_ctx_destroy(context->semantic_context);
    ps_global_registry_destroy(context->global_registry);
    memset(context, 0, sizeof(*context));
    return 0;
  }
  return 1;
}

int ag_compiler_context_activate(ag_compiler_context_t *context) {
  if (!context || !context->semantic_context || context->is_active) return 0;
  context->previous_semantic_context =
      ps_ctx_activate(context->semantic_context);
  context->previous_global_registry =
      ps_global_registry_activate(context->global_registry);
  context->is_active = 1;
  return 1;
}

void ag_compiler_context_deactivate(ag_compiler_context_t *context) {
  if (!context || !context->is_active) return;
  ps_global_registry_activate(context->previous_global_registry);
  ps_ctx_activate(context->previous_semantic_context);
  context->previous_global_registry = NULL;
  context->previous_semantic_context = NULL;
  context->is_active = 0;
}

void ag_compiler_context_dispose(ag_compiler_context_t *context) {
  if (!context) return;
  ag_compiler_context_deactivate(context);
  ps_ctx_destroy(context->semantic_context);
  ps_global_registry_destroy(context->global_registry);
  memset(context, 0, sizeof(*context));
}
