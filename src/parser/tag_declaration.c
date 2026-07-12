#include "tag_declaration.h"

#include "diag.h"

void psx_apply_parsed_tag_declaration(
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok) {
  psx_tag_declaration_resolution_t resolution;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = kind,
          .name = name,
          .name_len = name_len,
          .mode = mode,
          .member_count = member_count,
          .size = size,
          .alignment = alignment,
      },
      &resolution);
  if (resolution.status == PSX_TAG_DECLARATION_OK) return;
  if (resolution.status == PSX_TAG_DECLARATION_REDEFINITION) {
    psx_diag_ctx(diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで再定義されています (C11 6.7.2)",
                 name_len, name);
  }
  if (resolution.status == PSX_TAG_DECLARATION_KIND_CONFLICT) {
    psx_diag_ctx(diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで異なる種類として宣言されています (C11 6.7.2.3)",
                 name_len, name);
  }
  psx_diag_ctx(diag_tok, "tag",
               "canonical tag declaration resolution failed for '%.*s'",
               name_len, name);
}
