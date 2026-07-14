#ifndef SEMANTIC_TYPEDEF_DECLARATION_RESOLUTION_H
#define SEMANTIC_TYPEDEF_DECLARATION_RESOLUTION_H

#include "../parser/type.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_global_registry_t psx_global_registry_t;

typedef enum {
  PSX_TYPEDEF_DECLARATION_OK = 0,
  PSX_TYPEDEF_DECLARATION_INVALID,
  PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT,
  PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT,
  PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT,
  PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT,
} psx_typedef_declaration_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  char *name;
  int name_len;
  const psx_type_t *type;
} psx_typedef_declaration_resolution_request_t;

typedef struct {
  psx_typedef_declaration_status_t status;
  int created;
  int redeclared;
  int scope_depth;
} psx_typedef_declaration_resolution_t;

void psx_resolve_typedef_declaration(
    const psx_typedef_declaration_resolution_request_t *request,
    psx_typedef_declaration_resolution_t *resolution);

#endif
