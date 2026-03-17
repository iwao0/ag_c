#ifndef PARSER_SWITCH_CTX_H
#define PARSER_SWITCH_CTX_H

#include "../tokenizer/token.h"

void psw_push_ctx(void);
void psw_pop_ctx(void);
void psw_register_case(long long v, token_t *tok);
void psw_register_default(token_t *tok);
int psw_has_ctx(void);

#endif

