#include "static_assert_declaration.h"

#include "runtime_context.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <string.h>

static token_t *current_token(
    psx_parser_runtime_context_t *runtime_context) {
  return tk_get_current_token_ctx(
      ps_parser_runtime_tokenizer(runtime_context));
}

void psx_parse_static_assert_syntax_with_context(
    psx_parsed_static_assert_declaration_t *declaration,
    const psx_static_assert_syntax_context_t *context) {
  psx_parser_runtime_context_t *runtime_context =
      context ? context->runtime_context : NULL;
  if (!declaration || !runtime_context ||
      !context->parse_assignment_expression ||
      !ps_parser_runtime_tokenizer(runtime_context))
    return;
  tokenizer_context_t *tokenizer_context =
      ps_parser_runtime_tokenizer(runtime_context);
  ag_diagnostic_context_t *diagnostics =
      ps_parser_runtime_diagnostics(runtime_context);
  memset(declaration, 0, sizeof(*declaration));
  declaration->diagnostic_token = current_token(runtime_context);
  if (current_token(runtime_context)->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED,
        current_token(runtime_context),
                   "%s",
                   diag_message_for_in(
                       diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  tk_set_current_token_ctx(
      tokenizer_context, current_token(runtime_context)->next);
  tk_expect_ctx(tokenizer_context, '(');
  declaration->condition =
      context->parse_assignment_expression(context->context);
  tk_expect_ctx(tokenizer_context, ',');
  if (current_token(runtime_context)->kind != TK_STRING) {
    diag_emit_tokf_in(diagnostics,
                   DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING,
                   current_token(runtime_context), "%s",
                   diag_message_for_in(
                       diagnostics,
                       DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
  }
  do {
    tk_set_current_token_ctx(
        tokenizer_context, current_token(runtime_context)->next);
  } while (current_token(runtime_context)->kind == TK_STRING);
  tk_expect_ctx(tokenizer_context, ')');
  tk_expect_ctx(tokenizer_context, ';');
}
