#include "declarator_syntax.h"

#include "../tokenizer/tokenizer.h"

static token_t *current_token(void) { return tk_get_current_token(); }

typedef struct {
  token_ident_t *name;
  int pointer_count;
} declarator_parse_result_t;

static declarator_parse_result_t parse_declarator_recursive(
    const psx_declarator_syntax_t *syntax, int nesting_depth) {
  psx_skip_gnu_attributes();
  unsigned char pointer_const[24] = {0};
  unsigned char pointer_volatile[24] = {0};
  int pointer_count = 0;
  while (tk_consume('*')) {
    if (pointer_count >= 24) {
      if (syntax->diagnose_too_complex)
        syntax->diagnose_too_complex(syntax->context, current_token());
      return (declarator_parse_result_t){0};
    }
    while (current_token()->kind == TK_CONST ||
           current_token()->kind == TK_VOLATILE ||
           current_token()->kind == TK_RESTRICT) {
      if (current_token()->kind == TK_CONST)
        pointer_const[pointer_count] = 1;
      if (current_token()->kind == TK_VOLATILE)
        pointer_volatile[pointer_count] = 1;
      tk_set_current_token(current_token()->next);
    }
    pointer_count++;
    psx_skip_gnu_attributes();
  }

  token_ident_t *name = NULL;
  int direct_was_parenthesized = 0;
  int direct_pointer_count = 0;
  int is_grouping = current_token()->kind == TK_LPAREN &&
                    (!syntax->is_grouping_parenthesis ||
                     syntax->is_grouping_parenthesis(
                         syntax->context, nesting_depth));
  if (is_grouping && tk_consume('(')) {
    direct_was_parenthesized = 1;
    declarator_parse_result_t direct =
        parse_declarator_recursive(syntax, nesting_depth + 1);
    name = direct.name;
    direct_pointer_count = direct.pointer_count;
    tk_expect(')');
  } else {
    name = tk_consume_ident();
    if (!name && syntax->require_identifier &&
        syntax->diagnose_missing_identifier) {
      syntax->diagnose_missing_identifier(syntax->context, current_token());
    }
  }

  psx_skip_gnu_attributes();
  while (syntax->consume_suffix &&
         syntax->consume_suffix(syntax->context, nesting_depth,
                                direct_was_parenthesized,
                                direct_pointer_count, pointer_count)) {
  }
  for (int i = pointer_count - 1; i >= 0; i--) {
    if (syntax->append_pointer &&
        !syntax->append_pointer(syntax->context, pointer_const[i],
                                pointer_volatile[i], nesting_depth)) {
      if (syntax->diagnose_too_complex)
        syntax->diagnose_too_complex(syntax->context, current_token());
      return (declarator_parse_result_t){
          .name = name,
          .pointer_count = direct_pointer_count + pointer_count,
      };
    }
  }
  return (declarator_parse_result_t){
      .name = name,
      .pointer_count = direct_pointer_count + pointer_count,
  };
}

token_ident_t *psx_parse_declarator_syntax(
    const psx_declarator_syntax_t *syntax, int *out_pointer_count) {
  if (!syntax) return NULL;
  declarator_parse_result_t result = parse_declarator_recursive(syntax, 0);
  if (out_pointer_count) *out_pointer_count = result.pointer_count;
  return result.name;
}
