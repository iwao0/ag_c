#ifndef SEMANTIC_TAG_DECLARATION_RESOLUTION_H
#define SEMANTIC_TAG_DECLARATION_RESOLUTION_H

#include "../parser/type.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef enum {
  PSX_TAG_DECLARATION_REFERENCE = 0,
  PSX_TAG_DECLARATION_FORWARD,
  PSX_TAG_DECLARATION_DEFINITION,
} psx_tag_declaration_mode_t;

typedef enum {
  PSX_TAG_DECLARATION_OK = 0,
  PSX_TAG_DECLARATION_INVALID,
  PSX_TAG_DECLARATION_REDEFINITION,
  PSX_TAG_DECLARATION_KIND_CONFLICT,
} psx_tag_declaration_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_local_registry_t *local_registry;
  token_kind_t kind;
  char *name;
  int name_len;
  psx_tag_declaration_mode_t mode;
  int member_count;
} psx_tag_declaration_resolution_request_t;

typedef struct {
  psx_tag_declaration_status_t status;
  int registered;
  int scope_depth;
} psx_tag_declaration_resolution_t;

void psx_resolve_tag_declaration(
    const psx_tag_declaration_resolution_request_t *request,
    psx_tag_declaration_resolution_t *resolution);

#endif
