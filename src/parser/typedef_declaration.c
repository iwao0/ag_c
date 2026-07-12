#include "typedef_declaration.h"

#include "diag.h"
#include "../semantic/typedef_declaration_resolution.h"

void psx_apply_parsed_typedef_declaration(
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok) {
  psx_typedef_declaration_resolution_t resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .name = name,
          .name_len = name_len,
          .type = type,
      },
      &resolution);
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OK) return;
  if (resolution.status == PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT) {
    psx_diag_ctx(diag_tok, "typedef",
                 "typedef名 '%.*s' の型が以前の宣言と異なります (C11 6.7p3)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT) {
    psx_diag_ctx(diag_tok, "typedef",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT) {
    psx_diag_ctx(diag_tok, "typedef",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT) {
    psx_diag_ctx(diag_tok, "typedef",
                 "'%.*s' はenum定数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  psx_diag_ctx(diag_tok, "typedef",
               "canonical typedef declaration resolution failed for '%.*s'",
               name_len, name);
}
