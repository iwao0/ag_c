#ifndef AST_H
#define AST_H

#include "../tokenizer/token.h"

// 抽象構文木 (AST) のノードの種類
typedef enum {
  ND_ADD,    // +
  ND_SUB,    // -
  ND_MUL,    // *
  ND_DIV,    // /
  ND_MOD,    // %
  ND_EQ,     // ==
  ND_NE,     // !=
  ND_LT,     // <
  ND_LE,     // <=
  ND_BITAND, // &
  ND_BITXOR, // ^
  ND_BITOR,  // |
  ND_SHL,    // <<
  ND_SHR,    // >>
  ND_LOGAND, // &&
  ND_LOGOR,  // ||
  ND_TERNARY, // ?:
  ND_COMMA,  // ,
  ND_ASSIGN, // =
  ND_LVAR,   // ローカル変数
  ND_IF,     // if
  ND_WHILE,  // while
  ND_DO_WHILE, // do ... while
  ND_FOR,    // for
  ND_SWITCH, // switch
  ND_CASE,   // case
  ND_DEFAULT, // default
  ND_BREAK,  // break
  ND_CONTINUE, // continue
  ND_GOTO,   // goto
  ND_LABEL,  // label:
  ND_PRE_INC, // ++x
  ND_PRE_DEC, // --x
  ND_POST_INC, // x++
  ND_POST_DEC, // x--
  ND_RETURN,  // return
  ND_BLOCK,   // { ... }
  ND_FUNCDEF, // 関数定義
  ND_FUNCALL, // 関数呼び出し
  ND_FUNCREF, // 関数シンボル参照（関数ポインタ値）
  ND_DEREF,   // 間接参照 (*p)
  ND_ADDR,    // アドレス取得 (&x)
  ND_STRING,  // 文字列リテラル
  ND_NUM,     // 整数
  ND_GVAR,    // グローバル変数参照
  ND_VLA_ALLOC, // VLA動的スタック確保: lhs=サイズ式(バイト), type_size=フレームオフセット
  ND_FP_TO_INT, // 浮動小数点 → 整数キャスト: lhs=FP式 (fp_kind が float/double を保持)
  ND_VA_ARG_AREA, // 識別子 `__va_arg_area`: stack 上の variadic 引数領域の先頭アドレス。
                  // stdarg.h の va_start マクロが参照する。codegen は x29 + STACK_SIZE を返す。
  ND_PTR_CAST,    // `(T*)expr` ポインタキャスト。codegen は lhs をそのまま評価する。
                  // node_mem_t の pointee_fp_kind 等を保持して、後段の deref に伝播させる。
} node_kind_t;

// 抽象構文木のノードの型
typedef struct node_t node_t;
struct node_t {
  node_kind_t kind; // ノードの型

  // ツリー構造用
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体

  // データ型判定用（演算結果の型）
  unsigned int fp_kind : 3;     // tk_float_kind_t (0..2)
  unsigned int is_unsigned : 1; // 1: unsigned演算
  unsigned int is_complex : 1;  // 1: _Complex型演算
  unsigned int is_atomic : 1;   // 1: _Atomic型（load-acquire/store-release）

  // 構造体戻り値サイズ（ND_RETURN: 関数の戻り値構造体サイズ, ND_FUNCALL: 呼出先の戻り値サイズ）
  int ret_struct_size;
};

// メモリ参照系ノード（型サイズ情報）
typedef struct node_mem_t node_mem_t;
struct node_mem_t {
  node_t base;
  short type_size;   // ロード/ストアサイズ（1=char, 8=int/pointer）
  short deref_size;  // ポインタが指す先の要素サイズ
  short base_deref_size; // 多段ポインタの最内ポインタが指す要素サイズ（int**→4）
  unsigned char bit_width;   // ビットフィールド幅（0: 非ビットフィールド, max 64）
  unsigned char bit_offset;  // ビットフィールド開始ビット位置（ストレージユニット先頭から）
  token_kind_t tag_kind; // TK_STRUCT/TK_UNION（非タグ型はTK_EOF）
  char *tag_name;
  int tag_len;
  unsigned int bit_is_signed : 1;           // ビットフィールドの符号（1: signed, 0: unsigned）
  unsigned int is_tag_pointer : 1;          // 1: tagへのポインタ値, 0: tag値そのもの
  unsigned int is_pointer : 1;              // 1: ポインタ型（ポインタ加算スケーリング対象）
  unsigned int is_unsigned : 1;             // 1: unsigned型
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_pointer_const_qualified : 1;
  unsigned int is_complex : 1;              // 1: _Complex型（実部+虚部）
  unsigned int is_atomic : 1;               // 1: _Atomic型（load-acquire/store-release）
  unsigned int pointee_is_void : 1;         // 1: pointee 型が void（`void *p`）
  unsigned int is_pointer_volatile_qualified : 1;
  unsigned int pointee_fp_kind : 3;         // tk_float_kind_t: ポインタ先スカラのFP種別
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;
  int pointer_qual_levels;
  // 多次元配列サポート用
  short inner_deref_size;       // サブスクリプト結果の deref_size（次元の要素サイズ。0=N/A）
  short next_deref_size;        // 3D 配列での 2 段サブスクリプト後の要素サイズ。0=2D 以下。
  // 4 次元以上の追加ストライド: サブスクリプト 1 回ごとに deref_size ← inner_deref_size,
  // inner_deref_size ← next_deref_size, next_deref_size ← extra_strides[0] と
  // シフトさせる。最大 8 次元（3 + 5 段）まで対応。
  int extra_strides[5];
  unsigned char extra_strides_count;
  int vla_row_stride_frame_off; // 2D VLA(内側も可変): 行ストライドを格納するフレームオフセット（0=コンパイル時定数）
};

// 数値ノード
typedef struct node_num_t node_num_t;
struct node_num_t {
  node_t base;
  long long val;    // 整数値
  double fval;      // 浮動小数点値
  int fval_id;      // 浮動小数点リテラルのID
  tk_float_suffix_kind_t float_suffix_kind;
};

// ローカル変数ノード
typedef struct node_lvar_t node_lvar_t;
struct node_lvar_t {
  node_mem_t mem;
  int offset;       // フレームオフセット
};

// 文字列リテラルノード
typedef struct node_string_t node_string_t;
struct node_string_t {
  node_mem_t mem;
  char *string_label; // 文字列リテラルのデータラベル
  tk_char_width_t char_width;
  tk_string_prefix_kind_t str_prefix_kind;
  int byte_len;       // 文字列の内容バイト数 (null 終端を含まない)。
                      // `char a[] = "hi"` で配列サイズを推論するのに使う。
};

// ブロックノード
typedef struct node_block_t node_block_t;
struct node_block_t {
  node_t base;
  node_t **body;    // ブロック内の文（NULL終端の動的配列）
};

// 関数ノード
typedef struct node_func_t node_func_t;
struct node_func_t {
  node_t base;
  node_t **args;    // 引数/仮引数の動的配列
  int nargs;        // 引数の数
  node_t *callee;   // 間接呼び出し時のcallee式（直接呼び出しはNULL）
  char *funcname;   // 関数名
  int funcname_len; // 関数名の長さ
  int is_variadic;  // 1: 可変長引数関数 (funcdef時のみ)
  // 関数定義のローカル変数連結リスト (next_all で辿る)。
  // 関数解析完了時に保存し、IR builder 等が後段で参照する。
  // 既存 AST 直 codegen には影響しない (未参照のまま動く)。
  struct lvar_t *lvars;
};

// 関数シンボル参照ノード
typedef struct node_funcref_t node_funcref_t;
struct node_funcref_t {
  node_t base;
  char *funcname;
  int funcname_len;
};

// 制御構造ノード
typedef struct node_ctrl_t node_ctrl_t;
struct node_ctrl_t {
  node_t base;
  node_t *els;      // else節（ND_IFのみ）
  node_t *init;     // 初期化式（ND_FORのみ）
  node_t *inc;      // インクリメント式（ND_FORのみ）
};

// case ラベルノード
typedef struct node_case_t node_case_t;
struct node_case_t {
  node_t base;
  long long val;    // case 値
  int label_id;     // codegenで使うラベル番号
};

// default ラベルノード
typedef struct node_default_t node_default_t;
struct node_default_t {
  node_t base;
  int label_id;     // codegenで使うラベル番号
};

// goto / label ノード
typedef struct node_jump_t node_jump_t;
struct node_jump_t {
  node_t base;
  char *name;
  int name_len;
  int label_id;     // codegenで解決されるラベル番号
};

// グローバル変数参照ノード
typedef struct node_gvar_t node_gvar_t;
struct node_gvar_t {
  node_mem_t mem;  // type_size, deref_size, tag info
  char *name;
  int name_len;
  unsigned int is_thread_local : 1;
};

// グローバル変数テーブル（連結リスト）
typedef struct global_var_t global_var_t;
struct global_var_t {
  global_var_t *next;
  char *name;
  int name_len;
  short type_size;    // sizeof（ロード/ストアサイズ）
  short deref_size;   // ポインタ先の要素サイズ
  unsigned int is_array : 1;       // 1: 配列
  unsigned int is_extern_decl : 1; // 1: extern宣言のみ（.comm不要）
  unsigned int has_init : 1;       // 1: 初期化子あり
  unsigned int is_thread_local : 1; // 1: _Thread_local
  unsigned int is_tag_pointer : 1;  // 1: tag へのポインタ (`struct P *pp`)
  // tag (struct / union) 情報。tag_kind == TK_EOF のとき非タグ型。
  // build_member_access が `gvar.member` でタグメンバを引くのに使う。
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  long long init_val; // 初期値（整数定数、スカラ用）
  char *init_symbol;  // アドレス初期化子のシンボル名（&g → "g"）
  int init_symbol_len;
  // 配列の `{...}` 初期化子: flat 化した値列を保持する。
  // 多次元 `{{1,2,3},{4,5,6}}` も行優先で平らに並べる。
  // init_count > 0 のとき codegen は init_values[] を要素サイズ単位で出力する。
  long long *init_values;
  int init_count;
  // 多次元配列の subscript strides (ローカル配列 lvar_t と同等の意味)。
  //   outer_stride: 1 次サブスクリプトのステップ (= 直下の次元 1 つ分のサイズ)
  //   mid_stride:   2 次サブスクリプトのステップ
  //   extra_strides: 3 次以降。N 次元配列なら N-1 個の stride をフラットに保持。
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  unsigned char extra_strides_count;
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
