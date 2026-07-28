#ifndef TOKENIZER_LITERALS_H
#define TOKENIZER_LITERALS_H

#include <stdbool.h>
#include <stdint.h>

#include "token.h"

typedef struct tokenizer_context_t tokenizer_context_t;

/** @brief Test whether input starts with a `\\uXXXX` / `\\UXXXXXXXX` UCN. */
bool tk_starts_with_ucn(const char *p, int *len);
/** @brief Parse a UCN into a code point. */
bool tk_parse_ucn_codepoint(const char *p, uint32_t *out, int *consumed);
/** @brief Test whether a UCN code point is permitted by C11. */
bool tk_is_valid_ucn_codepoint(uint32_t cp);
/** @brief Encode a Unicode code point as UTF-8. */
int tk_encode_utf8(uint32_t cp, char out[4]);

/** @brief Decode one UTF-8 sequence at s[*pos], advance *pos, and return its code point.
 * Invalid or incomplete sequences permissively return the raw byte. */
uint32_t tk_decode_utf8(const char *s, int len, int *pos);

/** @brief Convert the next string character to char_width code units in out[] and return the count (1-4).
 * Advance *pos by the consumed input.  Shared by emission, array
 * initialization, and element counting. */
int tk_next_string_code_units(
    const char *s, int len, int *pos, int char_width, uint32_t out[4]);
/** @brief Return the total unit count after converting a string to char_width code units. */
int tk_count_string_code_units(const char *s, int len, int char_width);
/** @brief Return the next raw/escaped element value for narrow-string initialization. */
uint32_t tk_next_narrow_string_code_unit(const char *s, int len, int *pos);
typedef void (*tk_string_code_unit_emit_fn)(uint32_t unit, void *user);
/** @brief Expand a string to char_width code units; max_units <= 0 means all units. */
int tk_emit_string_code_units(const char *s, int len, int char_width, int max_units,
                              tk_string_code_unit_emit_fn emit, void *user);
typedef void (*tk_string_literal_byte_emit_fn)(unsigned char byte, void *user);
/** @brief Expand a string literal to data bytes; if emit == NULL, only count bytes. */
int tk_emit_string_literal_bytes(const char *s, int len, int char_width,
                                 bool include_nul,
                                 tk_string_literal_byte_emit_fn emit, void *user);

/** @brief Read and return one escape value in a string or character constant. */
int tk_read_escape_char_ctx(tokenizer_context_t *ctx, char **pp);
/** @brief Skip one escape in a string or character constant without decoding it. */
void tk_skip_escape_in_literal_ctx(tokenizer_context_t *ctx, char **pp);

/** @brief Parse a string prefix (L/u/U/u8). */
void tk_parse_string_prefix(
    const char *p,
    int *prefix_len,
    tk_string_prefix_kind_t *prefix_kind,
    tk_char_width_t *char_width);
/** @brief Parse a character-constant prefix (L/u/U). */
void tk_parse_char_prefix(
    const char *p,
    int *prefix_len,
    tk_char_prefix_kind_t *prefix_kind,
    tk_char_width_t *char_width);
/** @brief Expand UCNs in an identifier to UTF-8. */
void tk_decode_identifier_ucn(
    tokenizer_context_t *context, char *start, int len,
    char **out_str, int *out_len, bool *has_ucn);

#endif
