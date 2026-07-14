#ifndef SEMANTIC_ENUM_CONSTANT_RESOLUTION_H
#define SEMANTIC_ENUM_CONSTANT_RESOLUTION_H

struct psx_parsed_enum_expr_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef enum {
  PSX_ENUM_CONSTANT_OK = 0,
  PSX_ENUM_CONSTANT_INVALID,
  PSX_ENUM_CONSTANT_DUPLICATE,
  PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT,
  PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT,
  PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT,
} psx_enum_constant_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_local_registry_t *local_registry;
  char *name;
  int name_len;
  long long value;
} psx_enum_constant_resolution_request_t;

typedef struct {
  psx_enum_constant_status_t status;
  int created;
  int scope_depth;
} psx_enum_constant_resolution_t;

void psx_resolve_enum_constant(
    const psx_enum_constant_resolution_request_t *request,
    psx_enum_constant_resolution_t *resolution);
long long psx_resolve_prepared_enum_const_expr(
    const struct psx_parsed_enum_expr_t *expression);
long long psx_resolve_prepared_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    const struct psx_parsed_enum_expr_t *expression);

#endif
