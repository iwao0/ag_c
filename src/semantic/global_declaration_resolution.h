#ifndef SEMANTIC_GLOBAL_DECLARATION_RESOLUTION_H
#define SEMANTIC_GLOBAL_DECLARATION_RESOLUTION_H

#include "../parser/symtab.h"

typedef enum {
  PSX_GLOBAL_DECLARATION_OK = 0,
  PSX_GLOBAL_DECLARATION_INVALID,
  PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT,
  PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT,
  PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT,
  PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT,
  PSX_GLOBAL_DECLARATION_TYPE_CONFLICT,
} psx_global_declaration_status_t;

typedef struct {
  char *name;
  int name_len;
  const psx_type_t *type;
  int is_extern_decl;
  int has_initializer;
} psx_global_declaration_resolution_request_t;

typedef struct {
  psx_global_declaration_status_t status;
  global_var_t *existing;
  int replace_existing_type;
  int clear_existing_extern;
} psx_global_declaration_resolution_t;

void psx_resolve_global_declaration(
    const psx_global_declaration_resolution_request_t *request,
    psx_global_declaration_resolution_t *resolution);

#endif
