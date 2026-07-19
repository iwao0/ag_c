#include "local_declaration_legacy.h"

#include "../parser/semantic_ctx.h"

void psx_frontend_init_local_declaration_callbacks_in_contexts(
    psx_frontend_legacy_local_declaration_adapter_t *adapter,
    psx_local_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context) {
  if (!adapter || !callbacks) return;
  *adapter = (psx_frontend_legacy_local_declaration_adapter_t){0};
  *callbacks = (psx_local_declaration_callbacks_t){0};
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return;
  psx_name_classifier_t source =
      ps_ctx_name_classifier(semantic_context);
  if (!psx_legacy_name_classifier_init(
          &adapter->name_classifier, &source, local_registry))
    return;
  psx_name_classifier_t classifier =
      psx_legacy_name_classifier_view(&adapter->name_classifier);
  psx_frontend_init_local_declaration_syntax_adapter(
      &adapter->syntax, callbacks, runtime_context, &classifier);
}
