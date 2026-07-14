#include "global_object_lowering.h"

#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "static_data_initializer.h"
#include "../diag/diag.h"
#include <stdlib.h>
#include <string.h>

int lower_resolved_global_object_declaration(
    const psx_resolved_global_object_request_t *request,
    psx_global_object_result_t *result) {
  if (!request || !result || !request->global_registry || !request->name ||
      request->name_len <= 0 || !request->type || !request->resolution ||
      request->resolution->status != PSX_GLOBAL_DECLARATION_OK) return 0;
  memset(result, 0, sizeof(*result));
  psx_global_registry_t *global_registry = request->global_registry;

  global_var_t *existing = request->resolution->existing;
  if (existing) {
    if (request->resolution->clear_existing_extern)
      existing->is_extern_decl = 0;
    if (request->resolution->complete_existing_array &&
        !ps_global_registry_complete_array_type(
            existing, request->type))
      return 0;
    result->global = existing;
    return 1;
  }

  global_var_t *global = calloc(1, sizeof(*global));
  if (!global) return 0;
  global->name = request->name;
  global->name_len = request->name_len;
  global->is_extern_decl = request->is_extern_decl ? 1 : 0;
  global->is_static = request->is_extern_decl ? 0
                                               : (request->is_static ? 1 : 0);
  if (!ps_global_registry_bind_decl_type(global, request->type)) {
    free(global);
    return 0;
  }
  ps_register_global_var_in(global_registry, global);
  result->global = global;
  result->created = 1;
  return 1;
}

static void diagnose_global_resolution(
    const psx_global_object_request_t *request,
    psx_global_declaration_status_t status) {
  switch (status) {
    case PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT:
      ps_diag_ctx(request->diag_tok, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
      return;
    case PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT:
      ps_diag_ctx(request->diag_tok, "decl",
                   "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                   request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT:
      ps_diag_ctx(request->diag_tok, "decl",
                   "'%.*s' は typedef 名として既に宣言されています (C11 6.7p4)",
                   request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT:
      ps_diag_ctx(request->diag_tok, "decl",
                   "'%.*s' は enum 定数として既に宣言されています (C11 6.7p4)",
                   request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_TYPE_CONFLICT:
      ps_diag_ctx(
          request->diag_tok, "decl",
          "グローバル変数 '%.*s' の型が以前の宣言と異なります (C11 6.7p4)",
          request->name_len, request->name);
      return;
    default:
      return;
  }
}

int lower_global_object_declaration(
    const psx_global_object_request_t *request,
    psx_global_object_result_t *result) {
  if (!request || !result || !request->semantic_context ||
      !request->global_registry) return 0;
  psx_global_declaration_resolution_t resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = request->semantic_context,
          .global_registry = request->global_registry,
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .is_extern_decl = request->is_extern_decl,
      },
      &resolution);
  if (resolution.status != PSX_GLOBAL_DECLARATION_OK) {
    diagnose_global_resolution(request, resolution.status);
    return 0;
  }
  return lower_resolved_global_object_declaration(
      &(psx_resolved_global_object_request_t){
          .global_registry = request->global_registry,
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .is_extern_decl = request->is_extern_decl,
          .is_static = request->is_static,
          .resolution = &resolution,
      },
      result);
}

int lower_resolved_global_declaration_initializer(
    global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok) {
  return lower_resolved_static_initializer(
      global, resolution, diag_tok, NULL);
}
