#ifndef PARSER_DECL_H
#define PARSER_DECL_H

#include "../ast.h"

typedef struct lvar_t lvar_t;
struct lvar_t {
  lvar_t *next;
  char *name;
  int len;
  int offset;
  int size;
  int elem_size;
  int is_array;
  int is_vla;          // 1: 可変長配列 (VLA) - offsetはベースポインタスロット
  int is_byref_param;  // 1: >16バイト構造体の値渡し仮引数 - フレームスロットはポインタ(8B)、elemは実際の構造体サイズ
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_tag_pointer;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_pointer_const_qualified;
  int is_pointer_volatile_qualified;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;
  int pointer_qual_levels;
  int align_bytes; // 0 = natural alignment
};

void psx_decl_reset_locals(void);
void psx_decl_reserve_variadic_regs(void);
lvar_t *psx_decl_find_lvar(char *name, int len);
lvar_t *psx_decl_register_lvar(char *name, int len);
lvar_t *psx_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array);
lvar_t *psx_decl_register_lvar_sized_align(char *name, int len, int size, int elem_size, int is_array, int align);

node_t *psx_decl_parse_declaration(void);
node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer,
                                              int is_const_qualified, int is_volatile_qualified);
node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer);

#endif
