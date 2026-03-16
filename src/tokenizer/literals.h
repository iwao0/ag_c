#ifndef TOKENIZER_LITERALS_H
#define TOKENIZER_LITERALS_H

#include <stdbool.h>
#include <stdint.h>

bool tk_starts_with_ucn(const char *p, int *len);
bool tk_parse_ucn_codepoint(const char *p, uint32_t *out, int *consumed);
bool tk_is_valid_ucn_codepoint(uint32_t cp);
int tk_encode_utf8(uint32_t cp, char out[4]);

int tk_read_escape_char(char **pp);
void tk_skip_escape_in_literal(char **pp);

void tk_parse_string_prefix(const char *p, int *prefix_len, int *prefix_kind, int *char_width);
void tk_parse_char_prefix(const char *p, int *prefix_len, int *prefix_kind, int *char_width);
void tk_decode_identifier_ucn(char *start, int len, char **out_str, int *out_len, bool *has_ucn);

#endif
