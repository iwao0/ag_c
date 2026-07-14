#ifndef AG_COMPILER_CONTEXT_H
#define AG_COMPILER_CONTEXT_H

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_semantic_context_t *previous_semantic_context;
  psx_global_registry_t *global_registry;
  psx_global_registry_t *previous_global_registry;
  psx_local_registry_t *local_registry;
  psx_local_registry_t *previous_local_registry;
  unsigned char is_active;
} ag_compiler_context_t;

int ag_compiler_context_init(ag_compiler_context_t *context);
int ag_compiler_context_is_complete(const ag_compiler_context_t *context);
int ag_compiler_context_activate(ag_compiler_context_t *context);
void ag_compiler_context_deactivate(ag_compiler_context_t *context);
void ag_compiler_context_dispose(ag_compiler_context_t *context);

#endif
