#ifndef PARSER_DECL_H
#define PARSER_DECL_H

#include "ast.h"

typedef struct lvar_t lvar_t;
struct lvar_t {
  lvar_t *next;
  char *name;
  int len;
  int offset;
  int size;
  int elem_size;
  int is_array;
  tk_float_kind_t fp_kind;
};

void pdecl_reset_locals(void);
lvar_t *pdecl_find_lvar(char *name, int len);
lvar_t *pdecl_register_lvar(char *name, int len);
lvar_t *pdecl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array);

node_t *pdecl_parse_declaration(void);
node_t *pdecl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind);

#endif

