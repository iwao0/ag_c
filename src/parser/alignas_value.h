#ifndef PARSER_ALIGNAS_VALUE_H
#define PARSER_ALIGNAS_VALUE_H

#include "../tokenizer/token.h"

int psx_parse_alignas_value(void);
int ps_eval_parsed_alignas_value(token_t *start, token_t *end);

#endif
