#ifndef PARSER_ALIGNAS_VALUE_H
#define PARSER_ALIGNAS_VALUE_H

#include "../tokenizer/token.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

int psx_parse_alignas_value_in_context(
    psx_semantic_context_t *semantic_context);
int psx_eval_parsed_alignas_value_in_context(
    psx_semantic_context_t *semantic_context,
    token_t *start, token_t *end);

#endif
