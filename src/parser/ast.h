#ifndef AST_H
#define AST_H

#include "../tokenizer/token.h"
#include "core.h"
#include "syntax_node_kind.h"
struct psx_parsed_type_name_t;
struct psx_parsed_local_declaration_t;
/* Symbol tables (global_var_t / string_lit_t / float_lit_t) were moved to
 * symtab.h in Phase C1.  ast.h defines AST nodes only.  Files that use symbol
 * table types must include symtab.h explicitly. */

// Abstract syntax tree node types.
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
  token_t *index_tok;
  node_t *index_expr;
  node_t *range_end_expr;
  char *member_name;
  int member_len;
  token_t *tok;
  token_t *member_tok;
  unsigned char is_range;
} psx_initializer_designator_t;

typedef struct {
  node_t *value;
  psx_initializer_designator_t *designators;
  int designator_count;
  char *member_name;
  int member_len;
  token_t *tok;
  token_t *value_tok;
  long long index;
  unsigned char has_index;
  unsigned char has_member;
} psx_initializer_entry_t;
struct node_t {
  psx_syntax_node_kind_t kind;

  // Tree structure.
  node_t *lhs;      // Left operand / condition.
  node_t *rhs;      // Right operand / then branch / loop body.
  token_t *tok;     // statement/expression start token for post-parse diagnostics
  token_kind_t source_op;

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
  token_t *type_name_token;
} node_compound_literal_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
} node_source_cast_t;

typedef struct {
  node_t base;
  char *member_name;
  int member_name_len;
  token_t *member_tok;
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
  token_t *operand_token;
  psx_type_name_ref_t type_name;
  unsigned char is_type_name;
} node_sizeof_query_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
} node_alignof_query_t;

typedef enum {
  PSX_OFFSETOF_DESIGNATOR_MEMBER = 0,
  PSX_OFFSETOF_DESIGNATOR_INDEX,
} psx_offsetof_designator_kind_t;

typedef struct {
  psx_offsetof_designator_kind_t kind;
  node_t *index_expression;
  char *member_name;
  int member_name_len;
  token_t *tok;
  token_t *member_tok;
} psx_offsetof_designator_t;

typedef struct {
  node_t base;
  psx_type_name_ref_t type_name;
  psx_offsetof_designator_t *designators;
  int designator_count;
} node_offsetof_query_t;

typedef struct {
  node_t base;
  psx_initializer_entry_t *entries;
  int entry_count;
} node_init_list_t;

typedef struct {
  node_t base;
  node_t *condition;
  token_t *condition_token;
} node_static_assert_t;

// Numeric node.
typedef struct node_num_t node_num_t;
struct node_num_t {
  node_t base;
  long long val;    // Integer value.
  double fval;      // Floating-point value.
  tk_float_suffix_kind_t float_suffix_kind;
};

// String-literal node.
typedef struct node_string_t node_string_t;
struct node_string_t {
  node_t base;
  char *literal_contents; // Source-derived contents including escapes.
  int literal_length;     // Raw byte length of literal_contents.
  tk_char_width_t char_width;
  tk_string_prefix_kind_t str_prefix_kind;
  int byte_len;       // Content byte count, excluding the null terminator.
                      // Used to infer the array size in `char a[] = "hi"`.
};

// Block node.
typedef struct node_block_t node_block_t;
struct node_block_t {
  node_t base;
  node_t **body;    // Statements in the block (NULL-terminated dynamic array).
};

// Function-call node.
typedef struct node_function_call_t node_function_call_t;
struct node_function_call_t {
  node_t base;
  node_t **arguments;
  int argument_count;
  node_t *callee;
};

// Control-flow node.
typedef struct node_ctrl_t node_ctrl_t;
struct node_ctrl_t {
  node_t base;
  node_t *els;      // Else branch (ND_IF only).
  node_t *init;     // Initializer (ND_FOR only).
  node_t *inc;      // Increment expression (ND_FOR only).
};

// Case-label node.
typedef struct node_case_t node_case_t;
struct node_case_t {
  node_t base;
  token_t *expression_token;
};

// Default-label node.
typedef struct node_default_t node_default_t;
struct node_default_t {
  node_t base;
};

// Goto / label node.
typedef struct node_jump_t node_jump_t;
struct node_jump_t {
  node_t base;
  char *name;
  int name_len;
  token_t *name_tok;
  unsigned scope_seq;
  unsigned declaration_seq;
};

/* global_var_t / string_lit_t / float_lit_t moved to symtab.h in Phase C1. */

#endif
