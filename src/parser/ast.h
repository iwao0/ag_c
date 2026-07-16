#ifndef AST_H
#define AST_H

#include "../tokenizer/token.h"
#include "../type_system/type_ids.h"
#include "core.h"
#include "syntax_node_kind.h"
#include "type.h"
struct lvar_t;
struct psx_lvar_usage_region_t;
struct psx_parsed_type_name_t;
struct psx_parsed_local_declaration_t;
struct psx_node_resolution_state_t;
/* シンボルテーブル (global_var_t / string_lit_t / float_lit_t) は symtab.h
 * へ分離済み (Phase C1)。ast.h は AST node 定義のみを担う。
 * symtab 型を使うファイルは symtab.h を個別に include すること。 */

// 抽象構文木のノードの型
typedef struct node_t node_t;
typedef struct {
  struct psx_parsed_type_name_t *syntax;
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
struct node_t {
  psx_work_node_kind_t kind;

  // ツリー構造用
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体
  token_t *tok;     // statement/expression start token for post-parse diagnostics
  struct psx_lvar_usage_region_t *usage_region;
  struct lvar_t *usage_lvar;
  token_kind_t source_op;

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

  /* Parser-created syntax nodes leave this NULL. Resolution working trees
   * attach separately owned semantic state. */
  struct psx_node_resolution_state_t *resolution_state;
};

typedef struct {
  node_t base;
  psx_decl_init_kind_t init_kind;
} node_decl_init_t;

typedef struct {
  node_t base;
  struct psx_parsed_local_declaration_t *declaration;
} node_local_declaration_t;

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
  unsigned char from_pointer;
} node_member_access_t;

typedef struct {
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
} node_generic_selection_t;

typedef struct {
  node_t base;
  node_t *operand;
  psx_type_name_ref_t type_name;
  unsigned char is_type_name;
} node_sizeof_query_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
} node_alignof_query_t;

typedef struct {
  node_t base;
  psx_initializer_entry_t *entries;
  int entry_count;
} node_init_list_t;

typedef struct {
  node_t base;
  node_t *condition;
} node_static_assert_t;

typedef struct {
  node_t base;
  struct psx_vla_runtime_plan_t *runtime_plan;
} node_vla_alloc_t;

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
  node_t base;
  int offset;       // フレームオフセット
  struct lvar_t *var; // 宣言元シンボル。semantic pass の symbol identity 判定に使う。
};

// 文字列リテラルノード
typedef struct node_string_t node_string_t;
struct node_string_t {
  node_t base;
  char *string_label; // 文字列リテラルのデータラベル
  char *literal_contents; // エスケープを含むソース由来の文字列内容。
  int literal_length;     // literal_contents の raw バイト長。
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

// 関数定義ノード
typedef struct node_function_definition_t node_function_definition_t;
struct node_function_definition_t {
  node_t base;
  node_t **parameters;
  int parameter_count;
  const psx_type_t *signature;
  psx_qual_type_t signature_qual_type;
  char *name;
  int name_len;
  int is_static;    // 1: static 関数 (内部リンケージ)。codegen で .global を抑制する。
  // 関数定義のローカル変数連結リスト (next_all で辿る)。
  // 関数解析完了時に保存し、IR builder 等が後段で参照する。
  // 既存 AST 直 codegen には影響しない (未参照のまま動く)。
  struct lvar_t *lvars;
};

// 関数呼び出しノード
typedef struct node_function_call_t node_function_call_t;
struct node_function_call_t {
  node_t base;
  node_t **arguments;
  int argument_count;
  node_t *callee;
  const psx_type_t *callee_type;
  psx_qual_type_t callee_qual_type;
  char *direct_name;
  int direct_name_len;
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
  struct global_var_t *symbol;
  char *name;
  int name_len;
  unsigned int is_thread_local : 1;
};

/* global_var_t / string_lit_t / float_lit_t は symtab.h に移動 (Phase C1)。 */

#endif
