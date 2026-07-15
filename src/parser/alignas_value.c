#include "alignas_value.h"
#include "enum_const.h"
#include "semantic_ctx.h"
#include "../target_info.h"
#include "../tokenizer/tokenizer.h"

static inline token_t *curtok(tokenizer_context_t *tokenizer_context) {
  return tk_get_current_token_ctx(tokenizer_context);
}

static inline void set_curtok(
    tokenizer_context_t *tokenizer_context, token_t *tok) {
  tk_set_current_token_ctx(tokenizer_context, tok);
}

int psx_parse_alignas_value_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context) {
  if (!semantic_context || !tokenizer_context) return 1;
  tk_expect_ctx(tokenizer_context, '(');
  int val = 1;
  if (psx_ctx_is_type_token(curtok(tokenizer_context)->kind) ||
      psx_ctx_is_typedef_name_token_in(
          semantic_context, curtok(tokenizer_context))) {
    int elem_size = 8;
    if (curtok(tokenizer_context)->kind == TK_IDENT) {
      token_ident_t *identifier =
          (token_ident_t *)curtok(tokenizer_context);
      psx_ctx_find_typedef_sizeof_in(
          semantic_context, identifier->str, identifier->len,
          &elem_size);
    } else {
      psx_ctx_get_type_info(
          curtok(tokenizer_context)->kind, NULL, &elem_size);
    }
    val = elem_size;
    while (curtok(tokenizer_context)->kind != TK_RPAREN &&
           curtok(tokenizer_context)->kind != TK_EOF) {
      if (curtok(tokenizer_context)->kind == TK_MUL) {
        val = ag_target_info_pointer_size(
            ps_ctx_target_info(semantic_context));
      }
      set_curtok(
          tokenizer_context, curtok(tokenizer_context)->next);
    }
  } else {
    long long v = psx_parse_enum_const_expr_in_contexts(
        semantic_context, tokenizer_context);
    val = (v > 0) ? (int)v : 1;
  }
  tk_expect_ctx(tokenizer_context, ')');
  return val;
}

int psx_eval_parsed_alignas_value_in_context(
    psx_semantic_context_t *semantic_context,
    token_t *start, token_t *end) {
  if (!start || !end) return 1;
  int value = 1;
  if (psx_ctx_is_type_token(start->kind) ||
      psx_ctx_is_typedef_name_token_in(
          semantic_context, start)) {
    int elem_size = 8;
    if (start->kind == TK_IDENT) {
      token_ident_t *identifier = (token_ident_t *)start;
      psx_ctx_find_typedef_sizeof_in(
          semantic_context, identifier->str, identifier->len,
          &elem_size);
    } else {
      psx_ctx_get_type_info(start->kind, NULL, &elem_size);
    }
    value = elem_size;
    for (token_t *token = start; token && token != end;
         token = token->next) {
      if (token->kind == TK_MUL) {
        value = ag_target_info_pointer_size(
            ps_ctx_target_info(semantic_context));
        break;
      }
    }
  } else {
    long long parsed = psx_eval_parsed_enum_const_expr_in_context(
        semantic_context, start, end);
    value = parsed > 0 ? (int)parsed : 1;
  }
  return value;
}
