#ifndef PARSER_INTERNAL_CORE_H
#define PARSER_INTERNAL_CORE_H

/* core.h は AST node 型を使わない (token_kind_t と bool のみ)。
 * Phase C1-2: ast.h ではなく token.h を直接 include する。 */
#include "ret_pointee_array.h"
#include "../tokenizer/token.h"
#include <stdbool.h>

#define PS_MAX_DECLARATOR_COUNT 1024
#define PS_MAX_INITIALIZER_ELEMENTS 4096

typedef struct {
  token_kind_t kind;
  int is_unsigned;
  int is_complex;
  int is_long_long;
  int is_plain_char;
  int is_long_double;
  int is_atomic;
  int is_thread_local;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_extern;
  int is_static;
  int alignas_value;
} psx_type_spec_result_t;

token_kind_t psx_consume_type_kind_ex(psx_type_spec_result_t *out);
/* _Generic 用: [start, end) のトークン綴りを単一スペースで連結 (skip は除外)。'(' を
 * 含まない単純型は NULL。複雑な派生型 (関数ポインタ/ネスト宣言子) の型照合に使う。 */
char *psx_serialize_decl_type_tokens(token_t *start, token_t *end, token_t *skip);
/* 単一識別子の制御式 `_Generic(var, ...)` の var の型シグネチャを名前で引く (無ければ NULL)。 */
char *psx_lookup_var_type_sig(char *name, int len);
/* グローバル変数の型シグネチャを記録する (トップレベル宣言から呼ぶ。翻訳単位を通じて永続)。 */
void psx_record_global_type_sig(char *name, int len, char *sig);
void psx_consume_pointer_prefix(int *is_ptr);
// `*` を消費しつつ段数を返す版 (多段ポインタ typedef の段数記録用)。
int psx_consume_pointer_prefix_counted(int *is_ptr);
bool psx_is_decl_prefix_token(token_kind_t k);
bool psx_is_gnu_attribute_token(const token_t *t);
void psx_skip_gnu_attributes(void);
void psx_skip_gnu_attributes_at(token_t **t);
typedef struct {
  int is_variadic;
  int nargs_fixed;
  unsigned short param_fp_mask;
  unsigned short param_int_mask;
} psx_funcptr_signature_t;

typedef struct {
  unsigned short param_fp_mask;
  unsigned short param_int_mask;
  unsigned char ret_int_width;
  tk_float_kind_t ret_fp_kind;
  tk_float_kind_t ret_pointee_fp_kind;
  psx_ret_pointee_array_t ret_pointee_array;
  int ret_is_void;
  int ret_is_data_pointer;
  int ret_is_funcptr;
  int ret_is_complex;
  int is_variadic;
  short nargs_fixed;
} psx_decl_funcptr_sig_t;

int psx_decl_funcptr_sig_has_payload(psx_decl_funcptr_sig_t sig);
void psx_funcptr_signature_reset(psx_funcptr_signature_t *sig);
void psx_skip_func_param_list(psx_funcptr_signature_t *sig);
void psx_skip_func_suffix_groups_ex(int *out_has_func_suffix,
                                    psx_funcptr_signature_t *sig);
bool psx_try_consume_pragma_pack_marker(void);

#endif
