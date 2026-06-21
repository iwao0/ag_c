#ifndef PARSER_SWITCH_CTX_H
#define PARSER_SWITCH_CTX_H

#include "../tokenizer/token.h"

void psx_switch_push_ctx(void);
void psx_switch_pop_ctx(void);
void psx_switch_register_case(long long v, token_t *tok);
void psx_switch_register_default(token_t *tok);
int psx_switch_has_ctx(void);

#endif

