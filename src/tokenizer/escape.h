#ifndef TOKENIZER_ESCAPE_H
#define TOKENIZER_ESCAPE_H

#include <stdint.h>

// Parse an escape sequence starting at s[*i] == '\\'.
// On success, advances *i and stores decoded value to *out, then returns 1.
// Returns 0 when no escape sequence starts at *i.
int tk_parse_escape_value(const char *s, int len, int *i, uint32_t *out);

#endif
