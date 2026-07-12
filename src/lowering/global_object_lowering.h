#ifndef LOWERING_GLOBAL_OBJECT_LOWERING_H
#define LOWERING_GLOBAL_OBJECT_LOWERING_H

#include "../parser/symtab.h"
#include "../semantic/global_declaration_plan.h"

typedef struct {
  char *name;
  int name_len;
  const psx_type_t *type;
  int is_extern_decl;
  int is_static;
  token_t *diag_tok;
} psx_global_object_request_t;

typedef struct {
  global_var_t *global;
  psx_global_storage_plan_t storage;
  int created;
} psx_global_object_result_t;

int lower_global_object_declaration(
    const psx_global_object_request_t *request,
    psx_global_object_result_t *result);

#endif
