#include "internal/array_suffixes.h"
#include "internal/diag.h"
#include "internal/enum_const.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }

int psx_parse_array_size_constexpr(void) {
  long long v = psx_parse_enum_const_expr();
  if (v <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }
  return (int)v;
}

int psx_parse_array_size_optional_constexpr(int *out_has_size) {
  if (tk_consume(']')) {
    if (out_has_size) *out_has_size = 0;
    return 0;
  }
  int n = psx_parse_array_size_constexpr();
  if (out_has_size) *out_has_size = 1;
  tk_expect(']');
  return n;
}

int psx_parse_member_array_suffixes(int *out_is_flex_array) {
  int arr_total = 1;
  int is_flex_array = 0;
  while (tk_consume('[')) {
    if (curtok()->kind == TK_RBRACKET) {
      is_flex_array = 1;
      arr_total = 0;
    } else {
      arr_total *= psx_parse_array_size_constexpr();
    }
    tk_expect(']');
  }
  if (out_is_flex_array) *out_is_flex_array = is_flex_array;
  return arr_total;
}
