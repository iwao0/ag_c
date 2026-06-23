#ifndef AST_H
#define AST_H

#include "../tokenizer/token.h"
/* シンボルテーブル (global_var_t / string_lit_t / float_lit_t) は symtab.h
 * へ分離済み (Phase C1)。ast.h は AST node 定義のみを担う。
 * symtab 型を使うファイルは symtab.h を個別に include すること。 */

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
  ND_INT_TO_FP, // 整数/別幅FP → 浮動小数点キャスト: lhs=式、fp_kind が変換先(float/double)を保持
  ND_FNEG,      // 浮動小数点の単項マイナス (-x): lhs=FP式、fp_kind が float/double を保持。
                // 符号ビット反転 (IR_FNEG)。`0.0 - x` だと -0.0 が +0.0 になるため専用ノード。
  ND_VA_ARG_AREA, // 識別子 `__va_arg_area`: stack 上の variadic 引数領域の先頭アドレス。
                  // stdarg.h の va_start マクロが参照する。codegen は x29 + STACK_SIZE を返す。
  ND_PTR_CAST,    // `(T*)expr` ポインタキャスト。codegen は lhs をそのまま評価する。
                  // node_mem_t の pointee_fp_kind 等を保持して、後段の deref に伝播させる。
  ND_CREAL,       // GNU __real__ x: 複素数 lhs の実部 (実数なら lhs)。fp_kind=結果型。
  ND_CIMAG,       // GNU __imag__ x: 複素数 lhs の虚部 (実数なら 0)。fp_kind=結果型。
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
  unsigned int from_logical_not : 1; // 1: 単項 `!x` を ND_EQ(x,0) に変換したノード
                                     // (`!p == 0` の precedence-trap 警告に使う)

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
  /* タグ宣言時のスコープ深度 + 1 (0=未設定、>0 で実 depth=値-1)。arena_alloc がゼロ
   * 初期化なので未設定を 0 にしておくと初期化忘れがあっても安全。メンバ参照経路で
   * 「変数が宣言時に見ていた tag」を引くのに使う。 */
  int tag_scope_depth_p1;
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
  unsigned int is_bool : 1;                  // 1: _Bool 型 (代入を 0/1 に正規化する)
  unsigned int is_long_long : 1;             // 1: long long 型 (_Generic で long と区別)
  unsigned int is_plain_char : 1;            // 1: plain char 型 (_Generic で signed/unsigned char と区別)
  unsigned int is_long_double : 1;           // 1: long double 型 (_Generic で double と区別。fp_kind は DOUBLE のまま)
  unsigned int pointee_is_bool : 1;          // 1: pointee 型が _Bool（_Bool 配列等）
  unsigned int pointee_is_unsigned : 1;      // 1: pointee 型が unsigned（unsigned 配列/ポインタ）
  // 配列要素 (各スロット) がスカラポインタ (`char *names[N]`, `int *vals[N]`) で
  // あることを示す。subscript の結果 ND_DEREF に is_scalar_ptr_member を
  // 立てるための上流フラグ。グローバル配列で deref_size = ポインタサイズ (8) と
  // pointee 要素サイズ (例: 1 for char) が異なるケースを表現する。
  unsigned int pointee_is_scalar_ptr : 1;
  unsigned int is_pointer_volatile_qualified : 1;
  unsigned int pointee_fp_kind : 3;         // tk_float_kind_t: ポインタ先スカラのFP種別
  // ポインタメンバ deref (`s.p` で p が `char *` 等のスカラポインタメンバ)
  // を表すフラグ。配列メンバの「decay 表現としての is_pointer」と区別する。
  // subscript_base_address_of がスカラポインタ deref の場合 ND_DEREF を返し
  // (= ポインタ値 load を引き起こす)、配列メンバの場合 ND_ADD (アドレス計算)
  // を返す挙動を切り替えるために使う。
  unsigned int is_scalar_ptr_member : 1;
  // 1: ND_PTR_CAST が「lhs を I64 へ zero-extend する」ラッパであることを示す。
  // `(long)unsigned_int` の zero-extend を IR_ZEXT で明示挿入するために使う
  // (coerce_to_type は常に SEXT のため unsigned の widen に乗れない)。
  unsigned int widen_zext_i64 : 1;
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
  int vla_row_stride_frame_off; // N-D VLA: 次 subscript で消費する runtime stride のフレームオフセット (0=なし)
  /* N-D VLA (N >= 3): vla_row_stride_frame_off の後にさらに何個の runtime stride スロット
   * が続くか。lvar_t と同じ意味。subscript で 1 段消費するたび -1、vla_row は +=8 シフトする。 */
  int vla_strides_remaining;
};

// 数値ノード
typedef struct node_num_t node_num_t;
struct node_num_t {
  node_t base;
  long long val;    // 整数値
  double fval;      // 浮動小数点値
  int fval_id;      // 浮動小数点リテラルのID
  tk_float_suffix_kind_t float_suffix_kind;
  // 整数リテラルが long / long long サフィックスを持つ (= 値が 32bit に収まっても
  // i64 として扱う)。`2L * u` が 32bit 演算で wrap するのを防ぐ。
  unsigned char int_is_long;
  // 整数リテラルが long long サフィックス (LL) を持つ。long と long long は同サイズ
  // でも別型 (C11 6.2.5) なので _Generic の型照合で区別する。
  unsigned char int_is_long_long;
  // 1: この NUM ノードが明示 cast (`(void*)0xdeadbeefL` 等) でポインタ型へ変換された
  // 結果。folding で ND_NUM に潰されてもキャスト経路を覚えておき、ポインタ変数初期化の
  // 制約チェック (C11 6.5.16.1) で「キャスト経由なら許容」として扱う。
  unsigned char from_pointer_cast;
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
  int is_static;    // 1: static 関数 (内部リンケージ)。codegen で .global を抑制する。
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

/* global_var_t / string_lit_t / float_lit_t は symtab.h に移動 (Phase C1)。 */

#endif
