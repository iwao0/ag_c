#include "static_assert_declaration.h"

#include "expr.h"
#include "local_registry.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <string.h>

static token_t *current_token(
    psx_parser_runtime_context_t *runtime_context) {
  return tk_get_current_token_ctx(
      ps_parser_runtime_tokenizer(runtime_context));
}

void psx_parse_static_assert_syntax_in_contexts(
    psx_parsed_static_assert_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!declaration || !semantic_context || !global_registry ||
      !local_registry || !runtime_context ||
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
  declaration->condition = psx_expr_assign_in_contexts(
      semantic_context, global_registry, local_registry,
      runtime_context,
      local_declarations);
  tk_expect_ctx(tokenizer_context, ',');
  if (current_token(runtime_context)->kind != TK_STRING) {
    diag_emit_tokf_in(diagnostics,
                   DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING,
                   current_token(runtime_context), "%s",
                   diag_message_for_in(
                       diagnostics,
                       DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
  }
  tk_set_current_token_ctx(
      tokenizer_context, current_token(runtime_context)->next);
  tk_expect_ctx(tokenizer_context, ')');
  tk_expect_ctx(tokenizer_context, ';');
}
