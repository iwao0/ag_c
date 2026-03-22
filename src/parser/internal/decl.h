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
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  unsigned int is_array : 1;
  unsigned int is_vla : 1;            // 1: 可変長配列 (VLA) - offsetはベースポインタスロット
  unsigned int is_byref_param : 1;    // 1: >16バイト構造体の値渡し仮引数 - フレームスロットはポインタ(8B)、elemは実際の構造体サイズ
  unsigned int is_tag_pointer : 1;
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_pointer_const_qualified : 1;
  unsigned int is_pointer_volatile_qualified : 1;
  unsigned int is_unsigned : 1;       // 1: unsigned type
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;
  int pointer_qual_levels;
  int align_bytes; // 0 = natural alignment
  // 多次元配列サポート用
  int outer_stride;             // 多次元配列の外側サブスクリプトストライド（内側次元のバイトサイズ）
  int vla_row_stride_frame_off; // 2D VLA(内側も可変): 行ストライドを格納するフレームオフセット（0=定数stride）
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
