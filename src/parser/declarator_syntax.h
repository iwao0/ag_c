#ifndef PARSER_DECLARATOR_SYNTAX_H
#define PARSER_DECLARATOR_SYNTAX_H

#include "core.h"

typedef struct arena_context_t arena_context_t;
typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

typedef struct {
  void *context;
  arena_context_t *arena_context;
  tokenizer_context_t *tokenizer_context;
  ag_diagnostic_context_t *diagnostic_context;
  int require_identifier;
  int (*consume_suffix)(void *context, int nesting_depth,
                        int direct_was_parenthesized,
                        int direct_pointer_count, int frame_pointer_count);
  int (*is_grouping_parenthesis)(void *context, int nesting_depth);
  int (*append_pointer)(void *context, int is_const_qualified,
                        int is_volatile_qualified,
                        int is_restrict_qualified, int nesting_depth);
  void (*diagnose_missing_identifier)(void *context, token_t *tok);
  void (*diagnose_too_complex)(void *context, token_t *tok);
} psx_declarator_syntax_t;

token_ident_t *psx_parse_declarator_syntax(
    const psx_declarator_syntax_t *syntax);

#endif
