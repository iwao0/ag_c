#ifndef LOWERING_LOCAL_OBJECT_LOWERING_H
#define LOWERING_LOCAL_OBJECT_LOWERING_H

#include "../parser/decl.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  const psx_type_t *type;
  int requested_alignment;
} psx_local_object_request_t;

lvar_t *lower_complete_local_object(
    const psx_local_object_request_t *request);
lvar_t *declare_incomplete_local_object(
    const psx_local_object_request_t *request);
int complete_declared_local_object(
    lvar_t *var, const psx_local_object_request_t *request);

#endif
