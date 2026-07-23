#include "initializer_syntax.h"

#include "core.h"
#include "diag.h"
#include "node_utils.h"
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

static inline tokenizer_context_t *tokenizer(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_tokenizer(runtime_context);
}

static inline token_t *curtok(
    psx_parser_runtime_context_t *runtime_context) {
  return tk_get_current_token_ctx(tokenizer(runtime_context));
}

static inline ag_diagnostic_context_t *diagnostics(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_diagnostics(runtime_context);
}

static void *grow_initializer_syntax_array(
    arena_context_t *arena_context, const void *items, int count,
    int *capacity, size_t item_size) {
  if (!arena_context || count < 0 || !capacity || item_size == 0)
    return NULL;
  if (count < *capacity) return (void *)items;
  if (*capacity < 0 || *capacity > INT_MAX / 2) return NULL;
  int next_capacity = *capacity > 0 ? *capacity * 2 : 8;
  if ((size_t)next_capacity > SIZE_MAX / item_size) return NULL;
  void *next = arena_alloc_in(
      arena_context, (size_t)next_capacity * item_size);
  if (!next) return NULL;
  if (items && count > 0)
    memcpy(next, items, (size_t)count * item_size);
  *capacity = next_capacity;
  return next;
}

void psx_prepare_optional_initializer_syntax(
    psx_parsed_initializer_t *out,
    psx_parser_runtime_context_t *runtime_context) {
  if (!out || !tokenizer(runtime_context)) return;
  *out = (psx_parsed_initializer_t){0};
  if (!curtok(runtime_context) ||
      curtok(runtime_context)->kind != TK_ASSIGN)
    return;
  out->has_initializer = 1;
  out->assign_tok = curtok(runtime_context);
  out->value_tok = curtok(runtime_context)->next;
}

void psx_parse_initializer_syntax_value_with_context(
    psx_parsed_initializer_t *out, token_t *assign_tok,
    const psx_initializer_syntax_context_t *context) {
  psx_parser_runtime_context_t *runtime_context =
      context ? context->runtime_context : NULL;
  if (!out || !runtime_context ||
      !context->parse_assignment_expression)
    return;
  *out = (psx_parsed_initializer_t){
      .has_initializer = 1,
      .kind = curtok(runtime_context)->kind == TK_LBRACE
                  ? PSX_DECL_INIT_LIST
                  : PSX_DECL_INIT_EXPR,
      .assign_tok = assign_tok,
      .value_tok = curtok(runtime_context),
  };
  out->value = out->kind == PSX_DECL_INIT_LIST
                   ? psx_parse_initializer_syntax_list_with_context(
                         context)
                   : context->parse_assignment_expression(
                         context->context);
}

node_t *psx_parse_initializer_syntax_list_with_context(
    const psx_initializer_syntax_context_t *context) {
  psx_parser_runtime_context_t *runtime_context =
      context ? context->runtime_context : NULL;
  if (!runtime_context || !context->parse_assignment_expression)
    return NULL;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  if (!tk_ctx) return NULL;
  token_t *brace_tok = curtok(runtime_context);
  tk_expect_ctx(tk_ctx, '{');
  if (curtok(runtime_context) &&
      curtok(runtime_context)->kind == TK_RBRACE) {
    diag_emit_tokf_in(
        diagnostics(runtime_context),
        DIAG_ERR_PARSER_EMPTY_INITIALIZER_LIST,
        curtok(runtime_context), "%s",
        diag_message_for_in(
            diagnostics(runtime_context),
            DIAG_ERR_PARSER_EMPTY_INITIALIZER_LIST));
  }
  psx_initializer_entry_t *entries = NULL;
  int count = 0;
  int capacity = 0;
  arena_context_t *arena_context =
      ps_parser_runtime_arena(runtime_context);
  if (!tk_consume_ctx(tk_ctx, '}')) {
    for (;;) {
      if (count >= PS_MAX_INITIALIZER_ELEMENTS) {
        ps_diag_ctx_in(
            diagnostics(runtime_context), curtok(runtime_context),
            "initializer-syntax",
            diag_message_for_in(
                diagnostics(runtime_context),
                DIAG_ERR_PARSER_INITIALIZER_ELEMENT_LIMIT_EXCEEDED),
            PS_MAX_INITIALIZER_ELEMENTS);
      }
      if (count == capacity) {
        psx_initializer_entry_t *next =
            grow_initializer_syntax_array(
                arena_context, entries, count, &capacity,
                sizeof(*entries));
        if (!next) {
          ps_diag_ctx_in(
              diagnostics(runtime_context), curtok(runtime_context),
              "initializer-syntax", "initializer allocation failed");
        }
        entries = next;
      }

      token_t *entry_tok = curtok(runtime_context);
      token_ident_t *member = NULL;
      psx_initializer_designator_t *designators = NULL;
      int designator_count = 0;
      int designator_capacity = 0;
      int has_index = 0;
      while (curtok(runtime_context)->kind == TK_DOT ||
             curtok(runtime_context)->kind == TK_LBRACKET) {
        if (designator_count == designator_capacity) {
          psx_initializer_designator_t *next =
              grow_initializer_syntax_array(
                  arena_context, designators, designator_count,
                  &designator_capacity, sizeof(*designators));
          if (!next)
            ps_diag_ctx_in(
                diagnostics(runtime_context), curtok(runtime_context),
                "initializer-syntax", "initializer allocation failed");
          designators = next;
        }
        token_t *designator_tok = curtok(runtime_context);
        if (tk_consume_ctx(tk_ctx, '.')) {
          token_ident_t *current_member = tk_consume_ident_ctx(tk_ctx);
          if (!current_member) {
            ps_diag_missing_in(
                diagnostics(runtime_context), curtok(runtime_context),
                diag_text_for_in(
                    diagnostics(runtime_context), DIAG_TEXT_MEMBER_NAME));
          }
          if (!member) member = current_member;
          designators[designator_count++] = (psx_initializer_designator_t){
              .kind = PSX_INIT_DESIGNATOR_MEMBER,
              .member_name = current_member->str,
              .member_len = current_member->len,
              .tok = designator_tok,
          };
        } else {
          tk_expect_ctx(tk_ctx, '[');
          node_t *index_expr =
              context->parse_assignment_expression(
                  context->context);
          node_t *range_end_expr = NULL;
          int is_range = 0;
          if (curtok(runtime_context)->kind == TK_ELLIPSIS) {
            if (context->diagnose_unsupported_gnu_extension)
              context->diagnose_unsupported_gnu_extension(
                  context->context, curtok(runtime_context),
                  "array range designator");
            tk_set_current_token_ctx(
                tk_ctx, curtok(runtime_context)->next);
            range_end_expr =
                context->parse_assignment_expression(
                    context->context);
            is_range = 1;
          }
          tk_expect_ctx(tk_ctx, ']');
          designators[designator_count++] = (psx_initializer_designator_t){
              .kind = PSX_INIT_DESIGNATOR_INDEX,
              .index_expr = index_expr,
              .range_end_expr = range_end_expr,
              .tok = designator_tok,
              .is_range = is_range,
          };
          has_index = 1;
        }
      }
      if (designator_count > 0) tk_expect_ctx(tk_ctx, '=');

      node_t *value = curtok(runtime_context)->kind == TK_LBRACE
                          ? psx_parse_initializer_syntax_list_with_context(
                                context)
                          : context->parse_assignment_expression(
                                context->context);
      entries[count++] = (psx_initializer_entry_t){
          .value = value,
          .member_name = member ? member->str : NULL,
          .member_len = member ? member->len : 0,
          .tok = entry_tok,
          .designators = designators,
          .designator_count = designator_count,
          .has_index = has_index,
          .has_member = member != NULL,
      };

      if (tk_consume_ctx(tk_ctx, '}')) break;
      tk_expect_ctx(tk_ctx, ',');
      if (tk_consume_ctx(tk_ctx, '}')) break;
    }
  }
  return psx_node_new_initializer_list_in(
      ps_parser_runtime_arena(runtime_context),
      entries, count, brace_tok);
}
