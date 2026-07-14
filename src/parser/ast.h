#ifndef AST_H
#define AST_H

#include "../tokenizer/token.h"
#include "core.h"
#include "type.h"
#include "vla_runtime.h"
struct lvar_t;
struct psx_lvar_usage_region_t;
struct psx_parsed_type_name_t;
struct tag_member_info_t;
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
  ND_IDENTIFIER, // raw identifier reference; semantic binding前のみ存在する
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
  ND_UNARY_NEGATE, // raw unary -operand。semantic lowering 前のみ存在する。
  ND_UNARY_DEREF, // raw unary *operand。semantic lowering 前のみ存在する。
  ND_SUBSCRIPT, // raw 添字式 base[index]。semantic lowering 前のみ存在する。
  ND_MEMBER_ACCESS, // raw base.member/base->member。semantic lowering 前のみ存在する。
  ND_GENERIC_SELECTION, // raw _Generic selection。semantic lowering 前のみ存在する。
  ND_SIZEOF_QUERY, // raw sizeof(type/expression)。semantic lowering 前のみ存在する。
  ND_ALIGNOF_QUERY, // raw _Alignof(type)。semantic lowering 前のみ存在する。
  ND_DEREF,   // 間接参照 (*p)
  ND_ADDR,    // アドレス取得 (&x)
  ND_STRING,  // 文字列リテラル
  ND_NUM,     // 整数
  ND_GVAR,    // グローバル変数参照
  ND_VLA_ALLOC, // VLA動的スタック確保: lhs=サイズ式(バイト)。descriptor 情報は node_utils 経由で読む。
  ND_FP_TO_INT, // 浮動小数点 → 整数キャスト: lhs=FP式 (fp_kind が float/double を保持)
  ND_INT_TO_FP, // 整数/別幅FP → 浮動小数点キャスト: lhs=式、fp_kind が変換先(float/double)を保持
  ND_FNEG,      // 浮動小数点の単項マイナス (-x): lhs=FP式、fp_kind が float/double を保持。
                // 符号ビット反転 (IR_FNEG)。`0.0 - x` だと -0.0 が +0.0 になるため専用ノード。
  ND_VA_ARG_AREA, // 識別子 `__va_arg_area`: stack 上の variadic 引数領域の先頭アドレス。
                  // stdarg.h の va_start マクロが参照する。codegen は x29 + STACK_SIZE を返す。
  ND_CAST,       // 明示 cast wrapper。pointer cast では pointee metadata を保持し、
                 // integer cast では operand を壊さず result 幅/signedness を保持する。
  ND_COMPOUND_LITERAL, // prepared syntax; semantic loweringでobjectを実体化
  ND_INIT_LIST, // raw braced initializer syntax
  ND_DECL_INIT, // raw declaration initializer, lowered after semantic resolution
  ND_CREAL,       // GNU __real__ x: 複素数 lhs の実部 (実数なら lhs)。fp_kind=結果型。
  ND_CIMAG,       // GNU __imag__ x: 複素数 lhs の虚部 (実数なら 0)。fp_kind=結果型。
  ND_STMT_EXPR,   // GNU statement expression ({ ...; expr })
} node_kind_t;

// 抽象構文木のノードの型
typedef struct node_t node_t;
typedef struct {
  struct psx_parsed_type_name_t *syntax;
  psx_type_t *bound_base_type;
  psx_type_t *resolved_type;
  unsigned scope_seq;
  unsigned declaration_seq;
} psx_type_name_ref_t;

typedef enum {
  PSX_DECL_INIT_EXPR,
  PSX_DECL_INIT_LIST,
} psx_decl_init_kind_t;

typedef enum {
  PSX_INIT_DESIGNATOR_MEMBER,
  PSX_INIT_DESIGNATOR_INDEX,
} psx_initializer_designator_kind_t;

typedef struct {
  psx_initializer_designator_kind_t kind;
  node_t *index_expr;
  node_t *range_end_expr;
  char *member_name;
  int member_len;
  token_t *tok;
  unsigned char is_range;
} psx_initializer_designator_t;

typedef struct {
  node_t *value;
  psx_initializer_designator_t designators[8];
  unsigned char designator_count;
  node_t *index_exprs[8];
  unsigned char index_expr_count;
  char *member_name;
  int member_len;
  token_t *tok;
  long long index;
  unsigned char has_index;
  unsigned char has_member;
} psx_initializer_entry_t;
/* Expression-local state that is independent of the canonical type. */
typedef struct {
  psx_vla_runtime_view_t vla_runtime;
  unsigned char is_scalar_ptr_member_lvalue;
  unsigned char subscript_uses_base_address;
  unsigned char bit_width;
  unsigned char bit_offset;
  unsigned char bit_is_signed;
} psx_expr_type_state_t;

struct node_t {
  node_kind_t kind; // ノードの型

  // ツリー構造用
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体
  token_t *tok;     // statement/expression start token for post-parse diagnostics
  struct psx_lvar_usage_region_t *usage_region;
  struct lvar_t *usage_lvar;
  token_kind_t source_op;

  unsigned int unsigned_override : 1;
  unsigned int has_unsigned_override : 1;
  unsigned int from_logical_not : 1; // 1: 単項 `!x` を ND_EQ(x,0) に変換したノード
                                     // (`!p == 0` の precedence-trap 警告に使う)
  unsigned int records_lvar_usage : 1;
  unsigned int lvar_usage_unevaluated : 1;
  unsigned int is_explicit_addr_expr : 1;
  unsigned int is_source_assignment : 1;
  unsigned int is_decl_initializer : 1;
  unsigned int has_empty_body : 1;
  unsigned int is_implicit_func_decl : 1;
  unsigned int is_implicit_int_return : 1;
  unsigned int widen_zext_i64 : 1;
  unsigned int is_source_cast : 1;
  unsigned int is_source_compound_assignment : 1;

  /* Canonical semantic type. */
  const psx_type_t *type;
  psx_expr_type_state_t type_state;
};

typedef struct {
  node_t base;
  psx_decl_init_kind_t init_kind;
} node_decl_init_t;

typedef struct {
  node_t base;
  char *name;
  int name_len;
  unsigned scope_seq;
  unsigned declaration_seq;
} node_identifier_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
  psx_type_t *object_type;
  unsigned char requires_addressable_object;
  unsigned char has_file_scope_storage;
} node_compound_literal_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
} node_source_cast_t;

typedef struct {
  node_t base;
  char *member_name;
  int member_name_len;
  struct tag_member_info_t *resolved_member;
  unsigned char from_pointer;
} node_member_access_t;

typedef struct {
  psx_type_t *type;
  psx_type_name_ref_t type_name;
  node_t *expression;
  token_t *tok;
  unsigned char is_default;
} psx_generic_association_t;

typedef struct {
  node_t base;
  node_t *control;
  psx_generic_association_t *associations;
  int association_count;
  int selected_index;
} node_generic_selection_t;

typedef struct {
  node_t base;
  node_t *operand;
  psx_type_t *queried_type;
  psx_type_name_ref_t type_name;
  node_t *runtime_size_expr;
  int resolved_size;
  int runtime_size_slot;
  unsigned char is_type_name;
  unsigned char evaluates_vla_operand;
} node_sizeof_query_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
  int resolved_alignment;
} node_alignof_query_t;

typedef struct {
  node_t base;
  psx_initializer_entry_t *entries;
  int entry_count;
} node_init_list_t;

typedef struct {
  node_t base;
  int descriptor_frame_off;
  int row_stride_frame_off;
} node_vla_alloc_t;

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
  // 明示 cast で得た整数定数の幅。0 は通常の int/long リテラル、1/2 は char/short。
  unsigned char int_width;
  // 明示 cast で得た plain char 定数。signed char / unsigned char と _Generic で区別する。
  unsigned char int_is_plain_char;
};

// ローカル変数ノード
typedef struct node_lvar_t node_lvar_t;
struct node_lvar_t {
  node_t base;
  int offset;       // フレームオフセット
  struct lvar_t *var; // 宣言元シンボル。semantic pass の symbol identity 判定に使う。
};

// 文字列リテラルノード
typedef struct node_string_t node_string_t;
struct node_string_t {
  node_t base;
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
  psx_type_t *function_type; // bound canonical callable type
  char *funcname;   // 関数名
  int funcname_len; // 関数名の長さ
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
  node_t base;
  char *name;
  int name_len;
  unsigned int is_thread_local : 1;
};

/* global_var_t / string_lit_t / float_lit_t は symtab.h に移動 (Phase C1)。 */

#endif
