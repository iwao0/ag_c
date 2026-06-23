#ifndef PARSER_STRUCT_LAYOUT_H
#define PARSER_STRUCT_LAYOUT_H

#include "../tokenizer/token.h"

typedef struct {
  token_ident_t *member;
  int is_ptr;
  int has_func_suffix;
  int paren_array_mul;
  /* `*` が `( ... )` の内側で消費されたか (例: `int (*p)[N]` や `int (*p)(...)` の `*`)。
   * 1 のとき、宣言子の trailing `[N]` (parens の外側) はメンバ自体の配列次元ではなく
   * pointee の配列次元として扱う (HANDOFF 続き69 → struct メンバ pointer-to-array 修正)。 */
  int ptr_in_paren;
} member_decl_head_t;

member_decl_head_t psx_parse_member_decl_head(void);

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size);

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size);

#endif
