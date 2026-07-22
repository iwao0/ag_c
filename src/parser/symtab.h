#ifndef SYMTAB_H
#define SYMTAB_H

/* parser のシンボル payload とリテラル表の定義。
 * global_var_t の identity と列挙順は scope graph が所有する。
 * AST node 定義は ast.h に残す。 */

#include "../tokenizer/token.h"
#include "gvar_public.h"
#include "literal_public.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

// グローバル変数の宣言 payload
//
// フィールドはアライメント降順 (8→4→2→1 バイト) に寄せ、真偽フラグをビットフィールドに
// 集約してパディングを抑えている。
struct global_var_t {
  // --- 8 バイト (ポインタ / long long / double) ---
  char *name;
  char *init_symbol;  // アドレス初期化子のシンボル名（&g → "g"）
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
  int *init_union_ordinals;  // 各 init slot の union active member ordinal (-1=未指定)
  int *init_offsets;  // static aggregate scalar leaf の object-relative byte offset
  long long init_val; // 初期値（整数定数、スカラ用）
  long long init_symbol_offset;  // `&a[1]` / `a+1` のシンボルからのバイトオフセット
  double fval;        // 浮動小数スカラの初期値 (fp_kind != NONE のとき有効)

  // --- 4 バイト (int / enum) ---
  int name_len;
  int init_symbol_len;
  int union_init_ordinal;  // union の designated 初期化 `{.m=v}` で活性メンバの序数 (既定 0=先頭)
  int init_count;
  int requested_alignment;
  // ビットフラグ群 (unsigned int コンテナ、4 バイト)。真偽フラグはここに集約する。
  unsigned int is_extern_decl : 1; // 1: extern宣言のみ（.comm不要）
  unsigned int is_static : 1;      // 1: static (内部リンケージ)。.global を出さず .comm でなく .zerofill に。
  unsigned int has_init : 1;       // 1: 初期化子あり
  unsigned int is_thread_local : 1; // 1: _Thread_local
  unsigned int is_compiler_generated : 1;
  unsigned int is_compound_literal : 1;
  const psx_semantic_type_table_t *decl_type_table;
  psx_qual_type_t decl_qual_type;
};
// 文字列リテラルテーブル（連結リスト）
struct string_lit_t {
  string_lit_t *next;
  char *label;
  char *str;
  int len;
  tk_char_width_t char_width;
  tk_string_prefix_kind_t str_prefix_kind;
};

// 浮動小数点リテラルテーブル（連結リスト）
// フィールドはアライメント降順 (8→4) に並べて内部パディングを除いている。
struct float_lit_t {
  float_lit_t *next;
  double fval;
  int id;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
};

#endif
