#include "initializer_syntax.h"

#include "core.h"
#include "diag.h"
#include "expr.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/literals.h"
#include <stdbool.h>
#include <stdlib.h>

static inline token_t *curtok(void) { return tk_get_current_token(); }

int psx_initializer_syntax_is_scalar_array_list(token_t *brace) {
  if (!brace || brace->kind != TK_LBRACE) return 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int at_entry_start = 1;
  for (token_t *tok = brace->next; tok; tok = tok->next) {
    if (tok->kind == TK_RBRACE && paren_depth == 0 && bracket_depth == 0)
      return 1;
    if (at_entry_start && paren_depth == 0 && bracket_depth == 0 &&
        tok->kind == TK_DOT) return 0;
    if (at_entry_start && paren_depth == 0 && bracket_depth == 0 &&
        tok->kind == TK_LBRACKET) {
      token_t *end = tok;
      for (;;) {
        int depth = 1;
        end = end->next;
        while (end && depth > 0) {
          if (end->kind == TK_ELLIPSIS) return 0;
          if (end->kind == TK_LBRACKET) depth++;
          else if (end->kind == TK_RBRACKET) depth--;
          if (depth > 0) end = end->next;
        }
        if (!end || !end->next) return 0;
        if (end->next->kind != TK_LBRACKET) break;
        end = end->next;
      }
      if (!end->next || end->next->kind != TK_ASSIGN) return 0;
    }
    if (tok->kind == TK_LPAREN) paren_depth++;
    else if (tok->kind == TK_RPAREN && paren_depth > 0) paren_depth--;
    else if (tok->kind == TK_LBRACKET) bracket_depth++;
    else if (tok->kind == TK_RBRACKET && bracket_depth > 0) bracket_depth--;
    if (tok->kind == TK_COMMA && paren_depth == 0 && bracket_depth == 0)
      at_entry_start = 1;
    else
      at_entry_start = 0;
  }
  return 0;
}

node_t *psx_parse_initializer_syntax_list(void) {
  token_t *brace_tok = curtok();
  tk_expect('{');
  psx_initializer_entry_t *entries = NULL;
  int count = 0;
  int capacity = 0;
  if (!tk_consume('}')) {
    for (;;) {
      if (count >= PS_MAX_INITIALIZER_ELEMENTS) {
        free(entries);
        psx_diag_ctx(curtok(), "initializer-syntax",
                     diag_message_for(DIAG_ERR_PARSER_INITIALIZER_ELEMENT_LIMIT_EXCEEDED),
                     PS_MAX_INITIALIZER_ELEMENTS);
      }
      if (count == capacity) {
        int next_capacity = capacity ? capacity * 2 : 8;
        psx_initializer_entry_t *next = realloc(
            entries, sizeof(*entries) * (size_t)next_capacity);
        if (!next) {
          free(entries);
          psx_diag_ctx(curtok(), "initializer-syntax",
                       "initializer allocation failed");
        }
        entries = next;
        capacity = next_capacity;
      }

      token_t *entry_tok = curtok();
      token_ident_t *member = NULL;
      psx_initializer_designator_t designators[8] = {0};
      int designator_count = 0;
      node_t *index_exprs[8] = {0};
      int index_expr_count = 0;
      while (curtok()->kind == TK_DOT || curtok()->kind == TK_LBRACKET) {
        if (designator_count >= 8) {
          free(entries);
          psx_diag_ctx(curtok(), "initializer-syntax", "%s",
                       diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
        }
        token_t *designator_tok = curtok();
        if (tk_consume('.')) {
          token_ident_t *current_member = tk_consume_ident();
          if (!current_member) {
            free(entries);
            psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
          }
          if (!member) member = current_member;
          designators[designator_count++] = (psx_initializer_designator_t){
              .kind = PSX_INIT_DESIGNATOR_MEMBER,
              .member_name = current_member->str,
              .member_len = current_member->len,
              .tok = designator_tok,
          };
        } else {
          tk_expect('[');
          node_t *index_expr = psx_expr_assign();
          node_t *range_end_expr = NULL;
          int is_range = 0;
          if (curtok()->kind == TK_ELLIPSIS) {
            psx_ctx_record_unsupported_gnu_extension_warning(
                curtok(), "array range designator");
            tk_set_current_token(curtok()->next);
            range_end_expr = psx_expr_assign();
            is_range = 1;
          }
          tk_expect(']');
          designators[designator_count++] = (psx_initializer_designator_t){
              .kind = PSX_INIT_DESIGNATOR_INDEX,
              .index_expr = index_expr,
              .range_end_expr = range_end_expr,
              .tok = designator_tok,
              .is_range = is_range,
          };
          index_exprs[index_expr_count++] = index_expr;
        }
      }
      if (designator_count > 0) tk_expect('=');

      node_t *value = curtok()->kind == TK_LBRACE
                          ? psx_parse_initializer_syntax_list()
                          : psx_expr_assign();
      entries[count++] = (psx_initializer_entry_t){
          .value = value,
          .member_name = member ? member->str : NULL,
          .member_len = member ? member->len : 0,
          .tok = entry_tok,
          .has_index = index_expr_count > 0,
          .has_member = member != NULL,
      };
      entries[count - 1].designator_count =
          (unsigned char)designator_count;
      for (int d = 0; d < designator_count; d++)
        entries[count - 1].designators[d] = designators[d];
      entries[count - 1].index_expr_count = (unsigned char)index_expr_count;
      for (int d = 0; d < index_expr_count; d++)
        entries[count - 1].index_exprs[d] = index_exprs[d];

      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  return psx_node_new_initializer_list(entries, count, brace_tok);
}

int psx_initializer_syntax_is_simple_member_list(token_t *brace) {
  if (!brace || brace->kind != TK_LBRACE) return 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int at_entry_start = 1;
  for (token_t *tok = brace->next; tok; tok = tok->next) {
    if (tok->kind == TK_LBRACE) return 0;
    if (tok->kind == TK_RBRACE && paren_depth == 0 && bracket_depth == 0)
      return 1;
    if (at_entry_start && paren_depth == 0 && bracket_depth == 0 &&
        tok->kind == TK_LBRACKET) return 0;
    if (at_entry_start && paren_depth == 0 && bracket_depth == 0 &&
        tok->kind == TK_DOT) {
      token_t *name = tok->next;
      if (!name || name->kind != TK_IDENT || !name->next ||
          name->next->kind != TK_ASSIGN) return 0;
    }
    if (tok->kind == TK_LPAREN) paren_depth++;
    else if (tok->kind == TK_RPAREN && paren_depth > 0) paren_depth--;
    else if (tok->kind == TK_LBRACKET) bracket_depth++;
    else if (tok->kind == TK_RBRACKET && bracket_depth > 0) bracket_depth--;
    if (tok->kind == TK_COMMA && paren_depth == 0 && bracket_depth == 0)
      at_entry_start = 1;
    else
      at_entry_start = 0;
  }
  return 0;
}

int psx_initializer_syntax_is_flat_mixed_list(token_t *brace) {
  if (!brace || brace->kind != TK_LBRACE) return 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  for (token_t *tok = brace->next; tok; tok = tok->next) {
    if (tok->kind == TK_LBRACE || tok->kind == TK_ELLIPSIS) return 0;
    if (tok->kind == TK_RBRACE && paren_depth == 0 && bracket_depth == 0)
      return 1;
    if (tok->kind == TK_LPAREN) paren_depth++;
    else if (tok->kind == TK_RPAREN && paren_depth > 0) paren_depth--;
    else if (tok->kind == TK_LBRACKET) bracket_depth++;
    else if (tok->kind == TK_RBRACKET && bracket_depth > 0) bracket_depth--;
  }
  return 0;
}

int psx_initializer_syntax_is_braced_subobject_array_list(token_t *brace) {
  if (!brace || brace->kind != TK_LBRACE) return 0;
  token_t *tok = brace->next;
  if (tok && tok->kind == TK_RBRACE) return 1;
  while (tok) {
    int has_designator = 0;
    while (tok->kind == TK_LBRACKET || tok->kind == TK_DOT) {
      has_designator = 1;
      if (tok->kind == TK_DOT) {
        tok = tok->next;
        if (!tok || tok->kind != TK_IDENT) return 0;
        tok = tok->next;
      } else {
        int depth = 1;
        tok = tok->next;
        while (tok && depth > 0) {
          if (tok->kind == TK_ELLIPSIS) return 0;
          if (tok->kind == TK_LBRACKET) depth++;
          else if (tok->kind == TK_RBRACKET) depth--;
          tok = tok->next;
        }
        if (!tok) return 0;
      }
    }
    if (has_designator) {
      if (!tok || tok->kind != TK_ASSIGN) return 0;
      tok = tok->next;
    }

    if (tok && tok->kind == TK_LBRACE) {
      int depth = 1;
      tok = tok->next;
      while (tok && depth > 0) {
        if (tok->kind == TK_LBRACE) depth++;
        else if (tok->kind == TK_RBRACE) depth--;
        tok = tok->next;
      }
      if (!tok) return 0;
    } else if (has_designator) {
      int paren_depth = 0;
      int bracket_depth = 0;
      while (tok) {
        if (paren_depth == 0 && bracket_depth == 0 &&
            (tok->kind == TK_COMMA || tok->kind == TK_RBRACE)) break;
        if (tok->kind == TK_LPAREN) paren_depth++;
        else if (tok->kind == TK_RPAREN && paren_depth > 0) paren_depth--;
        else if (tok->kind == TK_LBRACKET) bracket_depth++;
        else if (tok->kind == TK_RBRACKET && bracket_depth > 0)
          bracket_depth--;
        tok = tok->next;
      }
    } else {
      return 0;
    }

    if (!tok) return 0;
    if (tok->kind == TK_RBRACE) return 1;
    if (tok->kind != TK_COMMA) return 0;
    tok = tok->next;
    if (tok && tok->kind == TK_RBRACE) return 1;
  }
  return 0;
}

long long psx_initializer_syntax_count_brace_elements(token_t *brace) {
  if (!brace || brace->kind != TK_LBRACE) return 0;
  token_t *tok = brace->next;
  if (tok && tok->kind == TK_RBRACE) return 0;
  long long index = 0;
  long long max_seen = -1;
  int depth = 0;
  bool seen_content = false;
  while (tok) {
    if (depth == 0) {
      if (tok->kind == TK_RBRACE) {
        if (seen_content && index > max_seen) max_seen = index;
        break;
      }
      if (tok->kind == TK_COMMA) {
        if (seen_content) {
          if (index > max_seen) max_seen = index;
          index++;
          seen_content = false;
        }
        tok = tok->next;
        continue;
      }
      if (!seen_content && tok->kind == TK_LBRACKET) {
        tok = tok->next;
        if (tok && tok->kind == TK_NUM &&
            tk_as_num(tok)->num_kind == TK_NUM_KIND_INT) {
          index = (long long)tk_as_num_int(tok)->uval;
          tok = tok->next;
        } else {
          return 0;
        }
        if (!tok || tok->kind != TK_RBRACKET) return 0;
        tok = tok->next;
        if (tok && tok->kind == TK_ASSIGN) tok = tok->next;
        continue;
      }
    }
    if (tok->kind == TK_LBRACE || tok->kind == TK_LPAREN ||
        tok->kind == TK_LBRACKET) {
      depth++;
    } else if (tok->kind == TK_RBRACE || tok->kind == TK_RPAREN ||
               tok->kind == TK_RBRACKET) {
      depth--;
      if (depth < 0) return 0;
    }
    seen_content = true;
    tok = tok->next;
  }
  return max_seen < 0 ? 0 : max_seen + 1;
}

static long long count_string_units(token_t *tok, int require_rbrace) {
  long long total = 0;
  while (tok && tok->kind == TK_STRING) {
    token_string_t *string = (token_string_t *)tok;
    total += tk_count_string_code_units(
        string->str, string->len, (int)string->char_width);
    tok = tok->next;
  }
  if (require_rbrace && (!tok || tok->kind != TK_RBRACE)) return 0;
  return total + 1;
}

long long psx_initializer_syntax_infer_array_count(
    token_t *assign_tok, int element_size) {
  if (!assign_tok || assign_tok->kind != TK_ASSIGN) return 0;
  token_t *value = assign_tok->next;
  if (!value) return 0;
  if (value->kind == TK_STRING) {
    int width = (int)((token_string_t *)value)->char_width;
    if (width <= 0) width = 1;
    if (element_size == width) return count_string_units(value, 0);
  }
  if (value->kind == TK_LBRACE) {
    token_t *inside = value->next;
    if (inside && inside->kind == TK_STRING) {
      int width = (int)((token_string_t *)inside)->char_width;
      if (width <= 0) width = 1;
      if (element_size == width) {
        long long count = count_string_units(inside, 1);
        if (count > 0) return count;
      }
    }
  }
  return psx_initializer_syntax_count_brace_elements(value);
}

int psx_initializer_syntax_first_element_is_brace(token_t *assign_tok) {
  if (!assign_tok || assign_tok->kind != TK_ASSIGN) return 0;
  token_t *brace = assign_tok->next;
  return brace && brace->kind == TK_LBRACE && brace->next &&
         brace->next->kind == TK_LBRACE;
}

int psx_initializer_syntax_has_top_level_index_designator(
    token_t *assign_tok) {
  if (!assign_tok || assign_tok->kind != TK_ASSIGN || !assign_tok->next ||
      assign_tok->next->kind != TK_LBRACE) return 0;
  int depth = 0;
  int at_entry_start = 1;
  for (token_t *tok = assign_tok->next->next; tok; tok = tok->next) {
    if (depth == 0 && tok->kind == TK_RBRACE) return 0;
    if (depth == 0 && at_entry_start && tok->kind == TK_LBRACKET)
      return 1;
    if (tok->kind == TK_LBRACE || tok->kind == TK_LPAREN ||
        tok->kind == TK_LBRACKET) {
      depth++;
    } else if (tok->kind == TK_RBRACE || tok->kind == TK_RPAREN ||
               tok->kind == TK_RBRACKET) {
      if (depth > 0) depth--;
    }
    if (depth == 0 && tok->kind == TK_COMMA)
      at_entry_start = 1;
    else if (depth == 0)
      at_entry_start = 0;
  }
  return 0;
}
