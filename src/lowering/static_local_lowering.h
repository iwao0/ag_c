#ifndef LOWERING_STATIC_LOCAL_LOWERING_H
#define LOWERING_STATIC_LOCAL_LOWERING_H

#include "../parser/decl.h"
#include "../semantic/static_initializer_resolution.h"

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
  const psx_type_t *type;
} psx_static_local_object_request_t;

typedef struct {
  psx_static_local_kind_t kind;
  char *function_name;
  int function_name_len;
  char *name;
  int name_len;
  const psx_type_t *type;
  const psx_static_initializer_resolution_t *initializer_resolution;
  token_t *diag_tok;
} psx_static_local_declaration_request_t;

typedef struct {
  global_var_t *global;
  lvar_t *alias;
  int type_completed;
} psx_static_local_declaration_result_t;

void psx_static_local_lowering_reset(void);
int psx_static_local_prepare_global(global_var_t *global,
                                    const psx_type_t *type);
lvar_t *lower_static_local_object(
    const psx_static_local_object_request_t *request);
int lower_static_local_declaration(
    const psx_static_local_declaration_request_t *request,
    psx_static_local_declaration_result_t *result);
int lower_static_local_declaration_storage(
    const psx_static_local_declaration_request_t *request,
    psx_static_local_declaration_result_t *result);
int lower_static_local_declaration_initializer(
    global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok, int *type_completed);

#endif
