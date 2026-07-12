#include "enum_constant_declaration.h"

#include "diag.h"
#include "../semantic/enum_constant_resolution.h"

void psx_apply_parsed_enum_constant(
    char *name, int name_len, long long value, token_t *diag_tok) {
  psx_enum_constant_resolution_t resolution;
  psx_resolve_enum_constant(
      &(psx_enum_constant_resolution_request_t){
          .name = name,
          .name_len = name_len,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_ENUM_CONSTANT_OK) return;
  if (resolution.status == PSX_ENUM_CONSTANT_DUPLICATE) {
    psx_diag_duplicate_with_name(
        diag_tok, "enum constant", name, name_len);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT) {
    psx_diag_ctx(diag_tok, "enum",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT) {
    psx_diag_ctx(diag_tok, "enum",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT) {
    psx_diag_ctx(diag_tok, "enum",
                 "'%.*s' はtypedef名として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  psx_diag_ctx(diag_tok, "enum",
               "canonical enum constant resolution failed for '%.*s'",
               name_len, name);
}
