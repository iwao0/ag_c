#ifndef LOWERING_GLOBAL_OBJECT_LOWERING_H
#define LOWERING_GLOBAL_OBJECT_LOWERING_H

#include "../parser/ast.h"
#include "../parser/symtab.h"
#include "../semantic/global_declaration_resolution.h"
#include "../semantic/static_initializer_resolution.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  char *name;
  int name_len;
  psx_qual_type_t type;
  int is_extern_decl;
  int is_static;
  token_t *diag_tok;
} psx_global_object_request_t;

typedef struct {
  global_var_t *global;
  int created;
} psx_global_object_result_t;

int lower_global_object_declaration(
    const psx_global_object_request_t *request,
    psx_global_object_result_t *result);

typedef struct {
  psx_global_registry_t *global_registry;
  char *name;
  int name_len;
  int is_extern_decl;
  int is_static;
  int is_compiler_generated;
  const psx_global_declaration_resolution_t *resolution;
} psx_resolved_global_object_request_t;

int lower_resolved_global_object_declaration(
    const psx_resolved_global_object_request_t *request,
    psx_global_object_result_t *result);

int lower_resolved_global_declaration_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok);

#endif
