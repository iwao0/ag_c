#ifndef SEMANTIC_IDENTIFIER_RESOLUTION_H
#define SEMANTIC_IDENTIFIER_RESOLUTION_H

#include "../parser/local_registry.h"
#include "../parser/symtab.h"

typedef enum {
  PSX_IDENTIFIER_UNDEFINED = 0,
  PSX_IDENTIFIER_LOCAL,
  PSX_IDENTIFIER_ENUM_CONSTANT,
  PSX_IDENTIFIER_GLOBAL_OBJECT,
  PSX_IDENTIFIER_FUNCTION,
  PSX_IDENTIFIER_UNDECLARED_CALL,
} psx_identifier_resolution_kind_t;

typedef struct {
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
  const psx_type_t *function_type;
  long long enum_value;
  int parameter_count;
  int is_variadic;
} psx_identifier_resolution_t;

void psx_resolve_identifier(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_resolution_t *resolution);
global_var_t *psx_resolve_global_object_symbol(
    char *name, int name_len);

#endif
