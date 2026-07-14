#ifndef PARSER_LITERAL_PUBLIC_H
#define PARSER_LITERAL_PUBLIC_H

#include "../tokenizer/token.h"
#include <stdbool.h>

typedef struct string_lit_t string_lit_t;
typedef struct float_lit_t float_lit_t;
typedef void (*string_lit_visitor_t)(string_lit_t *lit, void *user);
typedef void (*float_lit_visitor_t)(float_lit_t *lit, void *user);

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

psx_string_lit_view_t ps_string_lit_view(const string_lit_t *lit);
psx_float_lit_view_t ps_float_lit_view(const float_lit_t *lit);
bool ps_iter_string_literals(string_lit_visitor_t fn, void *user);
bool ps_iter_float_literals(float_lit_visitor_t fn, void *user);
bool ps_has_string_literals(void);
bool ps_has_float_literals(void);

#endif
