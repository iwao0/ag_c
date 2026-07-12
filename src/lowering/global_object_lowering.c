#include "global_object_lowering.h"

#include "../parser/decl.h"
#include "../parser/diag.h"
#include "static_data_initializer.h"
#include "../diag/diag.h"
#include <stdlib.h>
#include <string.h>

int lower_resolved_global_object_declaration(
    const psx_resolved_global_object_request_t *request,
    psx_global_object_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type || !request->resolution ||
      request->resolution->status != PSX_GLOBAL_DECLARATION_OK) return 0;
  memset(result, 0, sizeof(*result));
  result->storage = request->resolution->storage;

  global_var_t *existing = request->resolution->existing;
  if (existing) {
    if (request->resolution->clear_existing_extern)
      existing->is_extern_decl = 0;
    if (request->resolution->replace_existing_type)
      psx_decl_set_gvar_decl_type(existing, request->type);
    if (existing->type_size == 0 && result->storage.storage_size > 0)
      psx_decl_set_gvar_type_size(existing, result->storage.storage_size);
    result->global = existing;
    return 1;
  }

  global_var_t *global = calloc(1, sizeof(*global));
  if (!global) return 0;
  global->name = request->name;
  global->name_len = request->name_len;
  global->type_size = result->storage.storage_size;
  global->is_extern_decl = request->is_extern_decl ? 1 : 0;
  global->is_static = request->is_extern_decl ? 0
                                               : (request->is_static ? 1 : 0);
  psx_decl_set_gvar_decl_type(global, request->type);
  psx_register_global_var(global);
  result->global = global;
  result->created = 1;
  return 1;
}

static void diagnose_global_resolution(
    const psx_global_object_request_t *request,
    psx_global_declaration_status_t status) {
  switch (status) {
    case PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT:
      psx_diag_ctx(request->diag_tok, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
      return;
    case PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT:
      psx_diag_ctx(request->diag_tok, "decl",
                   "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                   request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT:
      psx_diag_ctx(request->diag_tok, "decl",
                   "'%.*s' は typedef 名として既に宣言されています (C11 6.7p4)",
                   request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT:
      psx_diag_ctx(request->diag_tok, "decl",
                   "'%.*s' は enum 定数として既に宣言されています (C11 6.7p4)",
                   request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_TYPE_CONFLICT:
      psx_diag_ctx(
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
  if (!request || !result) return 0;
  psx_global_declaration_resolution_t resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
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
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .is_extern_decl = request->is_extern_decl,
          .is_static = request->is_static,
          .resolution = &resolution,
      },
      result);
}

int lower_global_declaration_initializer(
    global_var_t *global, psx_type_t *type,
    psx_decl_init_kind_t initializer_kind, node_t *initializer,
    token_t *diag_tok) {
  return lower_static_declaration_initializer(
      &(psx_static_declaration_initializer_request_t){
          .global = global,
          .type = type,
          .initializer_kind = initializer_kind,
          .initializer = initializer,
          .diag_tok = diag_tok,
      },
      NULL);
}
