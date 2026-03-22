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
} node_kind_t;

// 抽象構文木のノードの型
typedef struct node_t node_t;
struct node_t {
  node_kind_t kind; // ノードの型

  // ツリー構造用
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体

  // データ型判定用（演算結果の型）
  tk_float_kind_t fp_kind;

  // 構造体戻り値サイズ（ND_RETURN: 関数の戻り値構造体サイズ, ND_FUNCALL: 呼出先の戻り値サイズ）
  int ret_struct_size;
};

// メモリ参照系ノード（型サイズ情報）
typedef struct node_mem_t node_mem_t;
struct node_mem_t {
  node_t base;
  int type_size;   // ロード/ストアサイズ（1=char, 8=int/pointer）
  int deref_size;  // ポインタが指す先の要素サイズ
  int bit_width;   // ビットフィールド幅（0: 非ビットフィールド）
  int bit_offset;  // ビットフィールド開始ビット位置（ストレージユニット先頭から）
  int bit_is_signed; // ビットフィールドの符号（1: signed, 0: unsigned）
  token_kind_t tag_kind; // TK_STRUCT/TK_UNION（非タグ型はTK_EOF）
  char *tag_name;
  int tag_len;
  int is_tag_pointer; // 1: tagへのポインタ値, 0: tag値そのもの
  int is_pointer;     // 1: ポインタ型（ポインタ加算スケーリング対象）
  int is_unsigned;    // 1: unsigned型
  int is_const_qualified;
  int is_volatile_qualified;
  int is_pointer_const_qualified;
  int is_pointer_volatile_qualified;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;
  int pointer_qual_levels;
  // 多次元配列サポート用
  int inner_deref_size;         // サブスクリプト結果の deref_size（次元の要素サイズ。0=N/A）
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
};

// グローバル変数テーブル（連結リスト）
typedef struct global_var_t global_var_t;
struct global_var_t {
  global_var_t *next;
  char *name;
  int name_len;
  int type_size;      // sizeof（ロード/ストアサイズ）
  int deref_size;     // ポインタ先の要素サイズ
  int is_array;       // 1: 配列
  int is_extern_decl; // 1: extern宣言のみ（.comm不要）
  int has_init;       // 1: 初期化子あり
  long long init_val; // 初期値
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
