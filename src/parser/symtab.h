#ifndef SYMTAB_H
#define SYMTAB_H

/* parser のシンボルテーブル定義 (Phase C1 リファクタリング)。
 * グローバル変数 / 文字列リテラル / 浮動小数リテラルの連結リスト型と
 * その extern グローバルを集約。AST node 定義は ast.h に残す。 */

#include "../tokenizer/token.h"

// グローバル変数テーブル（連結リスト）
typedef struct global_var_t global_var_t;
struct global_var_t {
  global_var_t *next;
  char *name;
  int name_len;
  short type_size;    // sizeof（ロード/ストアサイズ）
  short deref_size;   // ポインタ先の要素サイズ
  // ポインタ配列 (`char *names[N]`) の場合、pointee 要素の素のサイズ (char なら 1)。
  // deref_size は ポインタサイズ (8) になるため、pointee 区別用に保持する。
  // 0 のときは「pointee がスカラポインタではない」(通常配列等) ことを意味する。
  short pointee_elem_size;
  unsigned int is_array : 1;       // 1: 配列
  unsigned int is_extern_decl : 1; // 1: extern宣言のみ（.comm不要）
  unsigned int has_init : 1;       // 1: 初期化子あり
  unsigned int is_thread_local : 1; // 1: _Thread_local
  unsigned int is_tag_pointer : 1;  // 1: tag へのポインタ (`struct P *pp`)
  unsigned int elem_is_bool : 1;    // 1: 要素型が _Bool (`_Bool a[N]`)。init_values を 0/1 に正規化。
  // tag (struct / union) 情報。tag_kind == TK_EOF のとき非タグ型。
  // build_member_access が `gvar.member` でタグメンバを引くのに使う。
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  long long init_val; // 初期値（整数定数、スカラ用）
  // 浮動小数スカラ用: 宣言型の fp_kind と初期値 fval。
  // fp_kind != TK_FLOAT_KIND_NONE のとき、codegen はビットパターンで出力する。
  // 配列は未対応 (今は init_values[] が long long のため)。
  unsigned char fp_kind;
  double fval;
  char *init_symbol;  // アドレス初期化子のシンボル名（&g → "g"）
  int init_symbol_len;
  long long init_symbol_offset;  // `&a[1]` / `a+1` のシンボルからのバイトオフセット
  int union_init_ordinal;  // union の designated 初期化 `{.m=v}` で活性メンバの序数 (既定 0=先頭)
  // 配列の `{...}` 初期化子: flat 化した値列を保持する。
  // 多次元 `{{1,2,3},{4,5,6}}` も行優先で平らに並べる。
  // init_count > 0 のとき codegen は init_values[] を要素サイズ単位で出力する。
  long long *init_values;
  // 浮動小数配列用 (要素型 float/double のときのみ非NULL)。fvalues[i] が真の値で、
  // codegen はこちらをビットパターンで出す。init_values[i] は未使用。
  double *init_fvalues;
  /* 各 init slot ごとのシンボル参照 (関数名 や グローバル変数名)。NULL なら数値。
   * `struct Op { int (*f)(int); } gop = {sq};` のような funcptr メンバ初期化で使う。 */
  char **init_value_symbols;
  int *init_value_symbol_lens;
  int init_count;
  // 多次元配列の subscript strides (ローカル配列 lvar_t と同等の意味)。
  //   outer_stride: 1 次サブスクリプトのステップ (= 直下の次元 1 つ分のサイズ)
  //   mid_stride:   2 次サブスクリプトのステップ
  //   extra_strides: 3 次以降。N 次元配列なら N-1 個の stride をフラットに保持。
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  unsigned char extra_strides_count;
  unsigned char is_bool;  // _Bool スカラ: 代入/初期化を 0/1 に正規化する
  unsigned char is_unsigned;  // unsigned スカラ: load を zero-extend (符号拡張しない)
};
extern global_var_t *global_vars;

// 文字列リテラルテーブル（連結リスト）
typedef struct string_lit_t string_lit_t;
struct string_lit_t {
  string_lit_t *next;
  char *label;
  char *str;
  int len;
  tk_char_width_t char_width;
  tk_string_prefix_kind_t str_prefix_kind;
};
extern string_lit_t *string_literals;

// 浮動小数点リテラルテーブル（連結リスト）
typedef struct float_lit_t float_lit_t;
struct float_lit_t {
  float_lit_t *next;
  int id;
  double fval;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
};
extern float_lit_t *float_literals;

#endif
