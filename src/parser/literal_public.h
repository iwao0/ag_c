#ifndef PARSER_LITERAL_PUBLIC_H
#define PARSER_LITERAL_PUBLIC_H

#include "../tokenizer/token.h"

typedef struct string_lit_t string_lit_t;
typedef struct float_lit_t float_lit_t;

typedef struct {
  char *label;
  char *str;
  int len;
  tk_char_width_t char_width;
} psx_string_lit_view_t;

typedef struct {
  double fval;
  int id;
  tk_float_kind_t fp_kind;
} psx_float_lit_view_t;

psx_string_lit_view_t psx_string_lit_view(const string_lit_t *lit);
psx_float_lit_view_t psx_float_lit_view(const float_lit_t *lit);

#endif
