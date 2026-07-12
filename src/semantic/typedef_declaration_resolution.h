#ifndef SEMANTIC_TYPEDEF_DECLARATION_RESOLUTION_H
#define SEMANTIC_TYPEDEF_DECLARATION_RESOLUTION_H

#include "../parser/type.h"

typedef enum {
  PSX_TYPEDEF_DECLARATION_OK = 0,
  PSX_TYPEDEF_DECLARATION_INVALID,
  PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT,
  PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT,
  PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT,
  PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT,
} psx_typedef_declaration_status_t;

typedef struct {
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
