#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "../../tokenizer/token.h"
#include <stdbool.h>

void psx_ctx_reset_function_scope(void);
void psx_ctx_reset_function_names(void);
void psx_ctx_enter_block_scope(void);
void psx_ctx_leave_block_scope(void);
void psx_ctx_register_goto_ref(char *name, int len, token_t *tok);
void psx_ctx_register_label_def(char *name, int len, token_t *tok);
void psx_ctx_validate_goto_refs(void);

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count);
int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len, int member_count, int tag_size);
int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len);
void psx_ctx_add_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                            char *member_name, int member_len, int offset,
                            int type_size, int deref_size, int array_len,
                            token_kind_t member_tag_kind, char *member_tag_name,
                            int member_tag_len, int member_is_tag_pointer);
void psx_ctx_add_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len, int offset,
                               int type_size, int deref_size, int array_len,
                               token_kind_t member_tag_kind, char *member_tag_name,
                               int member_tag_len, int member_is_tag_pointer,
                               int bit_width, int bit_offset, int bit_is_signed);
bool psx_ctx_get_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len,
                               int *out_bit_width, int *out_bit_offset, int *out_bit_is_signed);
/* struct/union メンバの float/double 種別を後付けで設定/取得する。
 * tag_member_t を増設せずに add_tag_member_bf の追加引数を避けるための分離 API。 */
void psx_ctx_set_tag_member_fp_kind(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len,
                                     tk_float_kind_t fp_kind);
tk_float_kind_t psx_ctx_get_tag_member_fp_kind(token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 char *member_name, int member_len);
bool psx_ctx_find_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                             char *member_name, int member_len,
                             int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                             token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                             int *out_member_tag_len, int *out_member_is_tag_pointer);
bool psx_ctx_get_tag_member_at(token_kind_t tag_kind, char *tag_name, int tag_len, int index,
                               char **out_member_name, int *out_member_len,
                               int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                               token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                               int *out_member_tag_len, int *out_member_is_tag_pointer);
/* enum 定数を登録する。重複なら 0、新規なら 1 を返す。
 * 呼び出し元で 0 のとき診断を出す。 */
int psx_ctx_define_enum_const(char *name, int len, long long value);
bool psx_ctx_find_enum_const(char *name, int len, long long *out_value);
/* typedef 名を登録する。戻り値 1 = 成功 (新規 or 互換な再宣言)、
 * 0 = 既存と型が異なる衝突。呼び出し元で 0 のとき診断を出す。 */
int psx_ctx_define_typedef_name(char *name, int len, token_kind_t base_kind, int elem_size,
                                 tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                 char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                 int pointee_const_qualified, int pointee_volatile_qualified,
                                 int is_unsigned);
bool psx_ctx_find_typedef_name(char *name, int len, token_kind_t *out_base_kind,
                               int *out_elem_size, tk_float_kind_t *out_fp_kind,
                               token_kind_t *out_tag_kind, char **out_tag_name,
                               int *out_tag_len, int *out_is_pointer,
                               int *out_pointee_const_qualified, int *out_pointee_volatile_qualified,
                               int *out_is_unsigned);
// 拡張版: typedef した型が配列型か (`typedef int row_t[3]` 等) と sizeof_size を取得する。
// `row_t *a` のような仮引数で正しい outer_stride を設定するために使う。
bool psx_ctx_find_typedef_name_ex(char *name, int len, token_kind_t *out_base_kind,
                                  int *out_elem_size, tk_float_kind_t *out_fp_kind,
                                  token_kind_t *out_tag_kind, char **out_tag_name,
                                  int *out_tag_len, int *out_is_pointer,
                                  int *out_pointee_const_qualified,
                                  int *out_pointee_volatile_qualified, int *out_is_unsigned,
                                  int *out_is_array, int *out_sizeof_size);
int psx_ctx_define_typedef_name_ex(char *name, int len, token_kind_t base_kind, int elem_size,
                                    tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                    char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                    int pointee_const_qualified, int pointee_volatile_qualified,
                                    int is_unsigned, int is_array);
// さらに拡張: 多次元 typedef 配列型の最外側 dim を取得する/渡す。
// `typedef int M[3][4]; M *p` で mid_stride = sizeof_size / first_dim = 16
// を計算するのに使う。
bool psx_ctx_find_typedef_name_ex2(char *name, int len, token_kind_t *out_base_kind,
                                   int *out_elem_size, tk_float_kind_t *out_fp_kind,
                                   token_kind_t *out_tag_kind, char **out_tag_name,
                                   int *out_tag_len, int *out_is_pointer,
                                   int *out_pointee_const_qualified,
                                   int *out_pointee_volatile_qualified, int *out_is_unsigned,
                                   int *out_is_array, int *out_sizeof_size,
                                   int *out_array_first_dim);
int psx_ctx_define_typedef_name_ex2(char *name, int len, token_kind_t base_kind, int elem_size,
                                     tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                     char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                     int pointee_const_qualified, int pointee_volatile_qualified,
                                     int is_unsigned, int is_array, int array_first_dim);
// ex3: 多次元 typedef 配列の全次元を保存。array_dims[0] が最も外側、count=次元数。
// array_dim_count=0 のときは互換用 (1 次元 or 非配列扱い)。
int psx_ctx_define_typedef_name_ex3(char *name, int len, token_kind_t base_kind, int elem_size,
                                     tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                     char *tag_name, int tag_len, int is_pointer, int sizeof_size,
                                     int pointee_const_qualified, int pointee_volatile_qualified,
                                     int is_unsigned, int is_array, int array_first_dim,
                                     const int *array_dims, int array_dim_count);
bool psx_ctx_find_typedef_name_ex3(char *name, int len, token_kind_t *out_base_kind,
                                   int *out_elem_size, tk_float_kind_t *out_fp_kind,
                                   token_kind_t *out_tag_kind, char **out_tag_name,
                                   int *out_tag_len, int *out_is_pointer,
                                   int *out_pointee_const_qualified,
                                   int *out_pointee_volatile_qualified, int *out_is_unsigned,
                                   int *out_is_array, int *out_sizeof_size,
                                   int *out_array_first_dim,
                                   int *out_array_dims, int *out_array_dim_count,
                                   int max_dims);
bool psx_ctx_find_typedef_sizeof(char *name, int len, int *out_sizeof_size);
bool psx_ctx_is_typedef_name_token(token_t *tok);
void psx_ctx_define_function_name(char *name, int len);
void psx_ctx_define_function_name_with_ret(char *name, int len, int ret_struct_size);
void psx_ctx_set_function_ret_tag(char *name, int len, token_kind_t tag_kind, char *tag_name, int tag_len);
bool psx_ctx_has_function_name(char *name, int len);
int psx_ctx_get_function_ret_struct_size(char *name, int len);
// 関数戻り値の浮動小数点種別 (float/double) を取得/設定する。
// `(int)func()` キャストで FP→int 変換 (fcvtzs) を挿入するために必要。
void psx_ctx_set_function_ret_fp_kind(char *name, int len, tk_float_kind_t fp_kind);
tk_float_kind_t psx_ctx_get_function_ret_fp_kind(char *name, int len);
// 関数が variadic (`...` を持つ) かどうかと固定引数の個数を保持する。
// Apple ARM64 ABI で variadic 引数を stack に積むため、呼び出し側 codegen が
// `nargs_fixed` を境に register / stack を切り替えるのに使う。
void psx_ctx_set_function_variadic(char *name, int len, int is_variadic, int nargs_fixed);
bool psx_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed);
/* 戻り値型が void かどうかを保持/問い合わせる。代入や初期化での
 * void 値使用 (C11 6.5.16 制約違反) の検出に使う。 */
void psx_ctx_set_function_ret_void(char *name, int len, int is_void);
bool psx_ctx_is_function_ret_void(char *name, int len);
/* 関数の戻り値型を track する。既存と異なる型なら 0 を返す。 */
int psx_ctx_track_function_ret_type(char *name, int len,
                                     token_kind_t ret_token_kind, int ret_is_pointer);
void psx_ctx_get_function_ret_tag(char *name, int len, token_kind_t *out_tag_kind,
                                  char **out_tag_name, int *out_tag_len);

bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
int psx_ctx_scalar_type_size(token_kind_t kind);
void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif
