#include "global_object_lowering.h"

#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../diag/diag.h"
#include <stdlib.h>
#include <string.h>

static int global_types_compatible(
    const psx_type_t *existing, const psx_type_t *incoming) {
  if (psx_type_shape_matches(existing, incoming)) return 1;
  if (!existing || !incoming || existing->kind != PSX_TYPE_ARRAY ||
      incoming->kind != PSX_TYPE_ARRAY ||
      (existing->array_len > 0 && incoming->array_len > 0)) {
    return 0;
  }
  return psx_type_shape_matches(existing->base, incoming->base);
}

static void validate_global_name(
    const psx_global_object_request_t *request) {
  if (ps_ctx_has_function_name(request->name, request->name_len)) {
    psx_diag_ctx(request->diag_tok, "decl",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 request->name_len, request->name);
  }
  psx_typedef_info_t typedef_info;
  if (psx_ctx_find_typedef_name(
          request->name, request->name_len, &typedef_info)) {
    psx_diag_ctx(request->diag_tok, "decl",
                 "'%.*s' は typedef 名として既に宣言されています (C11 6.7p4)",
                 request->name_len, request->name);
  }
  long long enum_value = 0;
  if (psx_ctx_find_enum_const(
          request->name, request->name_len, &enum_value)) {
    psx_diag_ctx(request->diag_tok, "decl",
                 "'%.*s' は enum 定数として既に宣言されています (C11 6.7p4)",
                 request->name_len, request->name);
  }
}

int lower_global_object_declaration(
    const psx_global_object_request_t *request,
    psx_global_object_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type) return 0;
  memset(result, 0, sizeof(*result));
  if (!psx_plan_global_object_storage(request->type, &result->storage))
    return 0;
  validate_global_name(request);

  global_var_t *existing = ps_find_global_var(
      request->name, request->name_len);
  if (existing) {
    const psx_type_t *existing_type = psx_gvar_get_decl_type(existing);
    if (!global_types_compatible(existing_type, request->type)) {
      psx_diag_ctx(
          request->diag_tok, "decl",
          "グローバル変数 '%.*s' の型が以前の宣言と異なります (C11 6.7p4)",
          request->name_len, request->name);
    }
    if (existing->is_extern_decl && !request->is_extern_decl)
      existing->is_extern_decl = 0;

    int existing_is_incomplete =
        existing_type && existing_type->kind == PSX_TYPE_ARRAY &&
        existing_type->array_len <= 0;
    if (existing_is_incomplete || !result->storage.is_incomplete_array) {
      psx_decl_set_gvar_decl_type(existing, request->type);
    }
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
