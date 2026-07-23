#ifndef SEMANTIC_GLOBAL_DECLARATION_RESOLUTION_H
#define SEMANTIC_GLOBAL_DECLARATION_RESOLUTION_H

#include "../parser/symtab.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;

typedef enum {
  PSX_GLOBAL_DECLARATION_OK = 0,
  PSX_GLOBAL_DECLARATION_INVALID,
  PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT,
  PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT,
  PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT,
  PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT,
  PSX_GLOBAL_DECLARATION_TYPE_CONFLICT,
  PSX_GLOBAL_DECLARATION_LINKAGE_CONFLICT,
  PSX_GLOBAL_DECLARATION_ALIGNMENT_CONFLICT,
  PSX_GLOBAL_DECLARATION_DEFINITION_ALIGNMENT_MISSING,
} psx_global_declaration_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  char *name;
  int name_len;
  psx_qual_type_t type;
  int is_extern_decl;
  int is_static;
  int has_initializer;
  int has_alignment_specifier;
  int requested_alignment;
} psx_global_declaration_resolution_request_t;

typedef struct {
  psx_global_declaration_status_t status;
  psx_qual_type_t declaration_qual_type;
  global_var_t *existing;
  int complete_existing_array;
  int adopt_composite_type;
  int clear_existing_extern;
} psx_global_declaration_resolution_t;

void psx_resolve_global_declaration(
    const psx_global_declaration_resolution_request_t *request,
    psx_global_declaration_resolution_t *resolution);

int psx_finalize_tentative_globals_in(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry);

#endif
