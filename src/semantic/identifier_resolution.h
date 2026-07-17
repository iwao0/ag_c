#ifndef SEMANTIC_IDENTIFIER_RESOLUTION_H
#define SEMANTIC_IDENTIFIER_RESOLUTION_H

#include "../parser/function_public.h"
#include "../parser/local_registry.h"
#include "../parser/symtab.h"
#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;

typedef enum {
  PSX_IDENTIFIER_UNDEFINED = 0,
  PSX_IDENTIFIER_LOCAL,
  PSX_IDENTIFIER_ENUM_CONSTANT,
  PSX_IDENTIFIER_GLOBAL_OBJECT,
  PSX_IDENTIFIER_FUNCTION,
  PSX_IDENTIFIER_UNDECLARED_CALL,
} psx_identifier_resolution_kind_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  char *name;
  int name_len;
  int is_call;
  int has_local_lookup_point;
  psx_local_lookup_point_t local_lookup_point;
} psx_identifier_resolution_request_t;

typedef struct {
  psx_identifier_resolution_kind_t kind;
  lvar_t *local;
  global_var_t *global;
  const psx_function_symbol_t *function;
  long long enum_value;
} psx_identifier_resolution_t;

typedef struct {
  psx_identifier_resolution_t symbol;
  psx_qual_type_t declaration_qual_type;
  psx_qual_type_t expression_qual_type;
  global_var_t *static_storage_global;
  int decays_array_to_address;
  int decays_function_to_pointer;
  int local_has_static_storage;
  int local_is_vla;
} psx_identifier_expression_resolution_t;

void psx_resolve_identifier(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_resolution_t *resolution);
void psx_resolve_identifier_expression(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_expression_resolution_t *resolution);
global_var_t *psx_resolve_global_object_symbol_in(
    psx_global_registry_t *global_registry,
    char *name, int name_len);

#endif
