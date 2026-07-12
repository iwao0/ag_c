#ifndef SEMANTIC_ENUM_CONSTANT_RESOLUTION_H
#define SEMANTIC_ENUM_CONSTANT_RESOLUTION_H

typedef enum {
  PSX_ENUM_CONSTANT_OK = 0,
  PSX_ENUM_CONSTANT_INVALID,
  PSX_ENUM_CONSTANT_DUPLICATE,
  PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT,
  PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT,
  PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT,
} psx_enum_constant_status_t;

typedef struct {
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

#endif
