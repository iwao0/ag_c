#ifndef SYMTAB_H
#define SYMTAB_H

/* parser のシンボルテーブル定義 (Phase C1 リファクタリング)。
 * グローバル変数 / 文字列リテラル / 浮動小数リテラルの連結リスト型と
 * その extern グローバルを集約。AST node 定義は ast.h に残す。 */

#include "../tokenizer/token.h"

// グローバル変数テーブル（連結リスト）
//
// フィールドはアライメント降順 (8→4→2→1 バイト) に並べ、真偽フラグをビットフィールドに
// 集約してパディングを除いている (sizeof=152B / align 8、パディング 0。並べ替え前は 176B)。
// 検索ホットパスである try_build_global_var_node の線形走査は先頭の next/name/name_len
// のみ読むので、それらを最初のキャッシュラインに置いている。並べ替えはレイアウトのみの
// 変更で、全確保箇所が calloc + フィールド代入のため挙動には影響しない。
typedef struct global_var_t global_var_t;
struct global_var_t {
  // --- 8 バイト (ポインタ / long long / double) ---
  global_var_t *next;
  global_var_t *next_hash;  // 名前ハッシュ表のチェーン（psx_find_global_var の O(1) 化用）
  char *name;
  // tag (struct / union) 情報。tag_kind == TK_EOF のとき非タグ型。
  // build_member_access が `gvar.member` でタグメンバを引くのに使う。
  char *tag_name;
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
  long long init_val; // 初期値（整数定数、スカラ用）
  long long init_symbol_offset;  // `&a[1]` / `a+1` のシンボルからのバイトオフセット
  double fval;        // 浮動小数スカラの初期値 (fp_kind != NONE のとき有効)

  // --- 4 バイト (int / enum) ---
  int name_len;
  token_kind_t tag_kind;
  int tag_len;
  /* タグ宣言時のスコープ深度 + 1 (0=未設定の規約、>0 で実 depth=値-1)。
   * メンバ参照経路で「グローバル変数が宣言時に見ていた tag」のメンバを引くのに使う
   * (内側 shadow 内でグローバル変数のメンバアクセス対応)。 */
  int tag_scope_depth_p1;
  int init_symbol_len;
  int union_init_ordinal;  // union の designated 初期化 `{.m=v}` で活性メンバの序数 (既定 0=先頭)
  int init_count;
  // 多次元配列の subscript strides (ローカル配列 lvar_t と同等の意味)。
  //   outer_stride: 1 次サブスクリプトのステップ (= 直下の次元 1 つ分のサイズ)
  //   mid_stride:   2 次サブスクリプトのステップ
  //   extra_strides: 3 次以降。N 次元配列なら N-1 個の stride をフラットに保持。
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  // ビットフラグ群 (unsigned int コンテナ、4 バイト)。真偽フラグはここに集約する。
  unsigned int is_array : 1;       // 1: 配列
  unsigned int is_extern_decl : 1; // 1: extern宣言のみ（.comm不要）
  unsigned int is_static : 1;      // 1: static (内部リンケージ)。.global を出さず .comm でなく .zerofill に。
  unsigned int has_init : 1;       // 1: 初期化子あり
  unsigned int is_thread_local : 1; // 1: _Thread_local
  unsigned int is_tag_pointer : 1;  // 1: tag へのポインタ (`struct P *pp`)
  unsigned int elem_is_bool : 1;    // 1: 要素型が _Bool (`_Bool a[N]`)。init_values を 0/1 に正規化。
  unsigned int is_bool : 1;     // _Bool スカラ: 代入/初期化を 0/1 に正規化する
  unsigned int is_unsigned : 1; // unsigned スカラ: load を zero-extend (符号拡張しない)

  // --- 2 バイト (short) ---
  short type_size;    // sizeof（ロード/ストアサイズ）
  short deref_size;   // ポインタ先の要素サイズ
  // ポインタ配列 (`char *names[N]`) の場合、pointee 要素の素のサイズ (char なら 1)。
  // deref_size は ポインタサイズ (8) になるため、pointee 区別用に保持する。
  // 0 のときは「pointee がスカラポインタではない」(通常配列等) ことを意味する。
  short pointee_elem_size;

  // --- 1 バイト ---
  // 浮動小数スカラ用: 宣言型の fp_kind。fp_kind != TK_FLOAT_KIND_NONE のとき、
  // codegen は fval をビットパターンで出力する。配列は未対応 (init_values[] が long long)。
  unsigned char fp_kind;
  // 関数ポインタグローバル `double (*gops)(double)` の戻り型 fp_kind。
  // メンバ/ローカル funcptr と同じく、ポインタ自体の fp_kind は NONE のまま戻り fp を
  // ここに保持し、識別子解決時に ND_GVAR ノードの pointee_fp_kind へ伝播する。
  // これがないと `gops(x)` の funcall が戻り値を x0 で読み float/double が化ける。
  unsigned char pointee_fp_kind;
  unsigned char extra_strides_count;
  // 多段ポインタグローバル (`int **gp`) の段数。`*gp` が int* (8B) を返すよう、
  // pql>=2 のとき try_build_global_var_node が node の deref_size=8 /
  // base_deref_size=要素サイズ / pointer_qual_levels を立てる。単段/非ポインタは 0/1。
  unsigned char pointer_qual_levels;
};
extern global_var_t *global_vars;
/* global_vars への登録 (先頭 prepend + 名前索引へ挿入)。gv->name / gv->name_len は
 * 呼び出し前に設定済みであること。各登録経路はこれを通すこと。 */
void psx_register_global_var(global_var_t *gv);
/* 名前でグローバル変数を引く (見つからなければ NULL)。global_vars 線形走査の置換。 */
global_var_t *psx_find_global_var(char *name, int len);

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
// フィールドはアライメント降順 (8→4) に並べて内部パディングを除いている。
typedef struct float_lit_t float_lit_t;
struct float_lit_t {
  float_lit_t *next;
  double fval;
  int id;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
};
extern float_lit_t *float_literals;

#endif
