#ifndef PARSER_SYNTAX_NODE_KIND_H
#define PARSER_SYNTAX_NODE_KIND_H

/*
 * Parser-created Syntax AST node kinds.
 *
 * These describe source syntax only. Resolver-created storage references,
 * conversions, and other semantic nodes live in semantic/resolved_node_kind.h.
 */
typedef enum {
  PSX_SYNTAX_NODE_INVALID = -1,
  ND_ADD = 0, // +
  ND_SUB,    // -
  ND_MUL,    // *
  ND_DIV,    // /
  ND_MOD,    // %
  ND_EQ,     // ==
  ND_NE,     // !=
  ND_LT,     // <
  ND_LE,     // <=
  ND_GT,     // >
  ND_GE,     // >=
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
  ND_COMPOUND_ASSIGN, // +=, -=, *=, /=, %=, <<=, >>=, &=, ^=, |=
  ND_IDENTIFIER,
  ND_IF,
  ND_WHILE,
  ND_DO_WHILE,
  ND_FOR,
  ND_SWITCH,
  ND_CASE,
  ND_DEFAULT,
  ND_BREAK,
  ND_CONTINUE,
  ND_GOTO,
  ND_LABEL,
  ND_PRE_INC,
  ND_PRE_DEC,
  ND_POST_INC,
  ND_POST_DEC,
  ND_NULL_STMT,
  ND_RETURN,
  ND_BLOCK,
  ND_FUNCALL,
  ND_UNARY_PLUS,
  ND_UNARY_NEGATE,
  ND_LOGICAL_NOT,
  ND_BITWISE_NOT,
  ND_UNARY_DEREF,
  ND_SUBSCRIPT,
  ND_MEMBER_ACCESS,
  ND_GENERIC_SELECTION,
  ND_SIZEOF_QUERY,
  ND_ALIGNOF_QUERY,
  ND_ADDRESS_OF,
  ND_STRING,
  ND_NUM,
  ND_SOURCE_CAST,
  ND_COMPOUND_LITERAL,
  ND_INIT_LIST,
  ND_DECL_INIT,
  ND_LOCAL_DECLARATION,
  ND_STATIC_ASSERT,
  ND_CREAL,
  ND_CIMAG,
  ND_STMT_EXPR,
} psx_syntax_node_kind_t;

static inline int psx_syntax_node_kind_is_valid(
    psx_syntax_node_kind_t kind) {
  return kind >= ND_ADD && kind <= ND_STMT_EXPR;
}

#endif
