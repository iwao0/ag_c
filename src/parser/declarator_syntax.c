#include "declarator_syntax.h"

#include "arena.h"
#include "dynarray.h"
#include "../tokenizer/tokenizer.h"

#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

typedef struct {
  token_ident_t *name;
  int pointer_count;
} declarator_parse_result_t;

typedef struct {
  unsigned char is_const;
  unsigned char is_volatile;
} declarator_pointer_qualifiers_t;

static declarator_parse_result_t parse_declarator_recursive(
    const psx_declarator_syntax_t *syntax, int nesting_depth) {
  psx_skip_gnu_attributes();
  declarator_pointer_qualifiers_t *pointer_qualifiers = NULL;
  int pointer_count = 0;
  int pointer_capacity = 0;
  while (tk_consume('*')) {
    if (pointer_count == pointer_capacity) {
      int capacity = pda_next_cap(pointer_capacity, pointer_count + 1);
      declarator_pointer_qualifiers_t *qualifiers =
          arena_alloc_in(
              syntax->arena_context,
              (size_t)capacity * sizeof(*qualifiers));
      if (pointer_qualifiers && pointer_count > 0) {
        memcpy(qualifiers, pointer_qualifiers,
               (size_t)pointer_count * sizeof(*qualifiers));
      }
      pointer_qualifiers = qualifiers;
      pointer_capacity = capacity;
    }
    while (current_token()->kind == TK_CONST ||
           current_token()->kind == TK_VOLATILE ||
           current_token()->kind == TK_RESTRICT) {
      if (current_token()->kind == TK_CONST)
        pointer_qualifiers[pointer_count].is_const = 1;
      if (current_token()->kind == TK_VOLATILE)
        pointer_qualifiers[pointer_count].is_volatile = 1;
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
        !syntax->append_pointer(
            syntax->context, pointer_qualifiers[i].is_const,
            pointer_qualifiers[i].is_volatile, nesting_depth)) {
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
    const psx_declarator_syntax_t *syntax) {
  if (!syntax) return NULL;
  declarator_parse_result_t result = parse_declarator_recursive(syntax, 0);
  return result.name;
}
