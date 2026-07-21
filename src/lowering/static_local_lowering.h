#ifndef LOWERING_STATIC_LOCAL_LOWERING_H
#define LOWERING_STATIC_LOCAL_LOWERING_H

#include "../type_system/type_ids.h"
#include "static_data_initializer.h"

typedef struct lvar_t lvar_t;
typedef struct global_var_t global_var_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef enum {
  PSX_STATIC_LOCAL_SCALAR,
  PSX_STATIC_LOCAL_ARRAY,
  PSX_STATIC_LOCAL_CONSUMED_ARRAY,
  PSX_STATIC_LOCAL_AGGREGATE,
  PSX_STATIC_LOCAL_AGGREGATE_ARRAY,
  PSX_STATIC_LOCAL_KIND_COUNT,
} psx_static_local_kind_t;

typedef struct {
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  psx_static_local_kind_t kind;
  char *function_name;
  int function_name_len;
  char *name;
  int name_len;
  psx_qual_type_t type;
} psx_static_local_declaration_request_t;

typedef struct {
  global_var_t *global;
  lvar_t *alias;
} psx_static_local_declaration_result_t;

void psx_static_local_lowering_reset_in(
    psx_lowering_context_t *lowering_context);
int lower_static_local_declaration_storage(
    const psx_static_local_declaration_request_t *request,
    psx_static_local_declaration_result_t *result);
int lower_static_local_declaration_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_lowering_input_t *initializer,
    int *type_completed);

#endif
