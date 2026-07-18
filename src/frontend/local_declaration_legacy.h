#ifndef FRONTEND_LOCAL_DECLARATION_LEGACY_H
#define FRONTEND_LOCAL_DECLARATION_LEGACY_H

#include "local_declaration.h"
#include "../parser/name_classifier_legacy.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef struct {
  psx_frontend_local_declaration_syntax_adapter_t syntax;
  psx_legacy_name_classifier_t name_classifier;
} psx_frontend_legacy_local_declaration_adapter_t;

void psx_frontend_init_local_declaration_callbacks_in_contexts(
    psx_frontend_legacy_local_declaration_adapter_t *adapter,
    psx_local_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context);

#endif
