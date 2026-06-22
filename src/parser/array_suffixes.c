#include "array_suffixes.h"
#include "diag.h"
#include "enum_const.h"
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

int psx_parse_array_suffixes_constexpr_required(int base_mul) {
  int arr_total = (base_mul > 0) ? base_mul : 1;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = psx_parse_array_size_optional_constexpr(&has_size);
    if (has_size && n > 0) arr_total *= n;
  }
  return arr_total;
}

int psx_parse_array_suffixes_capture_dims(int base_mul, int *out_dims, int max_dims,
                                          int *out_dim_count) {
  int arr_total = (base_mul > 0) ? base_mul : 1;
  int dc = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = psx_parse_array_size_optional_constexpr(&has_size);
    if (has_size && n > 0) {
      arr_total *= n;
      if (out_dims && dc < max_dims) out_dims[dc] = n;
      dc++;
    }
  }
  if (out_dim_count) *out_dim_count = dc;
  return arr_total;
}

int psx_parse_member_array_suffixes(int *out_is_flex_array,
                                    int *out_dim_count, int *out_first_dim) {
  return psx_parse_member_array_suffixes_ex(out_is_flex_array, out_dim_count,
                                            out_first_dim, NULL, 0);
}

int psx_parse_member_array_suffixes_ex(int *out_is_flex_array,
                                       int *out_dim_count, int *out_first_dim,
                                       int *out_dims, int max_dims) {
  int arr_total = 1;
  int is_flex_array = 0;
  int dim_count = 0;
  int first_dim = 0;
  while (tk_consume('[')) {
    int dim;
    if (curtok()->kind == TK_RBRACKET) {
      is_flex_array = 1;
      arr_total = 0;
      dim = 0;
    } else {
      dim = psx_parse_array_size_constexpr();
      arr_total *= dim;
    }
    if (dim_count == 0) first_dim = dim;
    if (out_dims && dim_count < max_dims) out_dims[dim_count] = dim;
    dim_count++;
    tk_expect(']');
  }
  if (out_is_flex_array) *out_is_flex_array = is_flex_array;
  if (out_dim_count) *out_dim_count = dim_count;
  if (out_first_dim) *out_first_dim = first_dim;
  return arr_total;
}
