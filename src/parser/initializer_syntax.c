#include "initializer_syntax.h"

#include "core.h"
#include "diag.h"
#include "expr.h"
#include "local_registry.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

static inline token_t *curtok(void) { return tk_get_current_token(); }

void psx_prepare_optional_initializer_syntax(
    psx_parsed_initializer_t *out) {
  if (!out) return;
  *out = (psx_parsed_initializer_t){0};
  if (!curtok() || curtok()->kind != TK_ASSIGN) return;
  out->has_initializer = 1;
  out->assign_tok = curtok();
  out->value_tok = curtok()->next;
}

void psx_parse_initializer_syntax_value_in_contexts(
    psx_parsed_initializer_t *out, token_t *assign_tok,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!out || !semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return;
  *out = (psx_parsed_initializer_t){
      .has_initializer = 1,
      .kind = curtok()->kind == TK_LBRACE ? PSX_DECL_INIT_LIST
                                          : PSX_DECL_INIT_EXPR,
      .assign_tok = assign_tok,
      .value_tok = curtok(),
  };
  out->value = out->kind == PSX_DECL_INIT_LIST
                   ? psx_parse_initializer_syntax_list_in_contexts(
                         semantic_context, global_registry, local_registry,
                         runtime_context,
                         local_declarations)
                   : psx_expr_assign_in_contexts(
                         semantic_context, global_registry, local_registry,
                         runtime_context,
                         local_declarations);
}

node_t *psx_parse_initializer_syntax_list_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  token_t *brace_tok = curtok();
  tk_expect('{');
  psx_initializer_entry_t *entries = NULL;
  int count = 0;
  int capacity = 0;
  if (!tk_consume('}')) {
    for (;;) {
      if (count >= PS_MAX_INITIALIZER_ELEMENTS) {
        free(entries);
        ps_diag_ctx(curtok(), "initializer-syntax",
                     diag_message_for(DIAG_ERR_PARSER_INITIALIZER_ELEMENT_LIMIT_EXCEEDED),
                     PS_MAX_INITIALIZER_ELEMENTS);
      }
      if (count == capacity) {
        int next_capacity = capacity ? capacity * 2 : 8;
        psx_initializer_entry_t *next = realloc(
            entries, sizeof(*entries) * (size_t)next_capacity);
        if (!next) {
          free(entries);
          ps_diag_ctx(curtok(), "initializer-syntax",
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
          ps_diag_ctx(curtok(), "initializer-syntax", "%s",
                       diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
        }
        token_t *designator_tok = curtok();
        if (tk_consume('.')) {
          token_ident_t *current_member = tk_consume_ident();
          if (!current_member) {
            free(entries);
            ps_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
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
          node_t *index_expr = psx_expr_assign_in_contexts(
              semantic_context, global_registry, local_registry,
              runtime_context,
              local_declarations);
          node_t *range_end_expr = NULL;
          int is_range = 0;
          if (curtok()->kind == TK_ELLIPSIS) {
            ps_ctx_record_unsupported_gnu_extension_warning_in(
                semantic_context, curtok(), "array range designator");
            tk_set_current_token(curtok()->next);
            range_end_expr = psx_expr_assign_in_contexts(
                semantic_context, global_registry, local_registry,
                runtime_context,
                local_declarations);
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
                          ? psx_parse_initializer_syntax_list_in_contexts(
                                semantic_context, global_registry,
                                local_registry,
                                runtime_context,
                                local_declarations)
                          : psx_expr_assign_in_contexts(
                                semantic_context, global_registry,
                                local_registry,
                                runtime_context,
                                local_declarations);
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
