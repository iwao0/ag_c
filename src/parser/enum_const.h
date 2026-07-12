#ifndef PARSER_ENUM_CONST_H
#define PARSER_ENUM_CONST_H

#include "../tokenizer/token.h"

typedef struct {
  token_ident_t *enumerator;
  token_t *initializer_start;
  token_t *initializer_end;
} psx_parsed_enum_member_t;

typedef struct {
  psx_parsed_enum_member_t *members;
  int member_count;
  int member_capacity;
} psx_parsed_enum_body_t;

long long psx_parse_enum_const_expr(void);
long long psx_parse_case_const_expr(void);
long long ps_eval_parsed_enum_const_expr(token_t *start, token_t *end);
void psx_parse_enum_body(psx_parsed_enum_body_t *body);
int ps_apply_parsed_enum_body(const psx_parsed_enum_body_t *body);
void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body);
int psx_parse_enum_members(void);

#endif
