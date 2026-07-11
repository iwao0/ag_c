#ifndef LOWERING_STATIC_LOCAL_LOWERING_H
#define LOWERING_STATIC_LOCAL_LOWERING_H

#include "../parser/decl.h"

typedef enum {
  PSX_STATIC_LOCAL_SCALAR,
  PSX_STATIC_LOCAL_ARRAY,
  PSX_STATIC_LOCAL_CONSUMED_ARRAY,
  PSX_STATIC_LOCAL_AGGREGATE,
  PSX_STATIC_LOCAL_AGGREGATE_ARRAY,
  PSX_STATIC_LOCAL_KIND_COUNT,
} psx_static_local_kind_t;

typedef struct {
  psx_static_local_kind_t kind;
  char *function_name;
  int function_name_len;
  char *name;
  int name_len;
  global_var_t *global;
  int alias_size;
  int alias_element_size;
  const psx_type_t *type;
} psx_static_local_object_request_t;

typedef struct {
  char *function_name;
  int function_name_len;
  char *name;
  int name_len;
  int storage_size;
  int element_size;
  const psx_type_t *type;
  int has_initializer;
  long long integer_value;
  double floating_value;
  char *symbol;
  int symbol_len;
  long long symbol_offset;
} psx_static_local_scalar_request_t;

void psx_static_local_lowering_reset(void);
void psx_static_local_prepare_global(global_var_t *global,
                                     const psx_type_t *type);
lvar_t *lower_static_local_object(
    const psx_static_local_object_request_t *request);
lvar_t *lower_static_local_scalar(
    const psx_static_local_scalar_request_t *request);

#endif
