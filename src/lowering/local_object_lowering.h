#ifndef LOWERING_LOCAL_OBJECT_LOWERING_H
#define LOWERING_LOCAL_OBJECT_LOWERING_H

#include "../parser/decl.h"

typedef struct {
  char *name;
  int name_len;
  psx_type_t *type;
  int requested_alignment;
} psx_local_object_request_t;

typedef struct {
  lvar_t *var;
  int storage_size;
  int element_size;
  int alignment;
  int is_array;
  int type_attached;
} psx_local_object_result_t;

int lower_complete_local_object(
    const psx_local_object_request_t *request,
    psx_local_object_result_t *result);
int declare_incomplete_local_object(
    const psx_local_object_request_t *request,
    psx_local_object_result_t *result);
int complete_declared_local_object(
    lvar_t *var, const psx_local_object_request_t *request,
    psx_local_object_result_t *result);

#endif
