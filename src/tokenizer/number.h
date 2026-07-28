#ifndef TOKENIZER_NUMBER_H
#define TOKENIZER_NUMBER_H

#include <stdint.h>

#include "token.h"

typedef struct tokenizer_context_t tokenizer_context_t;

/* Intermediate representation for numeric-literal parsing.  It holds the
 * result of converting source text into a common integer/floating form;
 * token construction (tokenize_number_literal) copies it into a token.
 * Fields are ordered 8B, 4B (enum), then 1B to minimize internal padding
 * (sizeof = 48). */
struct parsed_num_t {
  long long val;
  unsigned long long uval;
  double fval;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
  tk_int_size_t int_size;
  tk_char_width_t char_width;
  tk_char_prefix_kind_t char_prefix_kind;
  bool is_unsigned;
  uint8_t int_base;
};
typedef struct parsed_num_t parsed_num_t;

/**
 * @brief Parse a numeric literal body into the common integer/floating representation (parsed_num_t).
 * @param pp Input cursor, advanced past the consumed text after parsing.
 * @param num Output for the parsed result.
 * @warning Invalid bases, suffixes, or out-of-range values terminate with a diagnostic.
 */
void tk_parse_number_literal_ctx(
    tokenizer_context_t *ctx, char **pp, parsed_num_t *num);

#endif
