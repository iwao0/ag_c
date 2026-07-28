#ifndef TOKENIZER_ESCAPE_PUBLIC_H
#define TOKENIZER_ESCAPE_PUBLIC_H

#include <stdint.h>

/**
 * @brief Decode an escape sequence.
 * @param s Input string.
 * @param len Input length.
 * @param i Start position (expects `s[*i] == '\\'`); advanced past the input on success.
 * @param out Decoded code point.
 * @return 1 on success, or 0 when the start position is not an escape.
 */
int tk_parse_escape_value(const char *s, int len, int *i, uint32_t *out);

#endif
