#ifndef AST_H
#define AST_H

#include "../tokenizer/token.h"
#include "core.h"
#include "syntax_node_kind.h"
struct psx_parsed_type_name_t;
struct psx_parsed_local_declaration_t;
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
  psx_syntax_node_kind_t kind;

  // ツリー構造用
  node_t *lhs;      // 左辺 / 条件式
  node_t *rhs;      // 右辺 / then節 / ループ本体
  token_t *tok;     // statement/expression start token for post-parse diagnostics
  token_kind_t source_op;

  unsigned int has_empty_body : 1;
  unsigned int is_source_cast : 1;
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

// 数値ノード
typedef struct node_num_t node_num_t;
struct node_num_t {
  node_t base;
  long long val;    // 整数値
  double fval;      // 浮動小数点値
  tk_float_suffix_kind_t float_suffix_kind;
};

// 文字列リテラルノード
typedef struct node_string_t node_string_t;
struct node_string_t {
  node_t base;
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

// 関数呼び出しノード
typedef struct node_function_call_t node_function_call_t;
struct node_function_call_t {
  node_t base;
  node_t **arguments;
  int argument_count;
  node_t *callee;
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
};

// default ラベルノード
typedef struct node_default_t node_default_t;
struct node_default_t {
  node_t base;
};

// goto / label ノード
typedef struct node_jump_t node_jump_t;
struct node_jump_t {
  node_t base;
  char *name;
  int name_len;
};

/* global_var_t / string_lit_t / float_lit_t は symtab.h に移動 (Phase C1)。 */

#endif
