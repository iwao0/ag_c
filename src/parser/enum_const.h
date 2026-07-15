#ifndef PARSER_ENUM_CONST_H
#define PARSER_ENUM_CONST_H

#include "../tokenizer/token.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct tokenizer_context_t tokenizer_context_t;

typedef enum {
  PSX_ENUM_EXPR_VALUE = 0,
  PSX_ENUM_EXPR_IDENTIFIER,
  PSX_ENUM_EXPR_UNARY,
  PSX_ENUM_EXPR_BINARY,
  PSX_ENUM_EXPR_CONDITIONAL,
} psx_parsed_enum_expr_kind_t;

typedef struct psx_parsed_enum_expr_t psx_parsed_enum_expr_t;
struct psx_parsed_enum_expr_t {
  psx_parsed_enum_expr_kind_t kind;
  token_kind_t op;
  long long value;
  char *identifier;
  int identifier_len;
  token_t *diagnostic_token;
  psx_parsed_enum_expr_t *lhs;
  psx_parsed_enum_expr_t *rhs;
  psx_parsed_enum_expr_t *alternative;
};

typedef struct {
  token_ident_t *enumerator;
  psx_parsed_enum_expr_t *initializer;
} psx_parsed_enum_member_t;

typedef struct {
  psx_parsed_enum_member_t *members;
  int member_count;
  int member_capacity;
} psx_parsed_enum_body_t;

long long psx_parse_enum_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context);
long long psx_parse_case_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context);
long long psx_eval_parsed_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    token_t *start, token_t *end);
void psx_parse_enum_body_in_contexts(
    psx_parsed_enum_body_t *body,
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context);
void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body);

#endif
