#include "static_assert_declaration.h"

#include "expr.h"
#include "local_registry.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

void psx_parse_static_assert_syntax(
    psx_parsed_static_assert_declaration_t *declaration) {
  psx_parse_static_assert_syntax_in_contexts(
      declaration, ps_ctx_active(), ps_local_registry_active(), NULL);
}

void psx_parse_static_assert_syntax_in_contexts(
    psx_parsed_static_assert_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!declaration || !semantic_context || !local_registry) return;
  memset(declaration, 0, sizeof(*declaration));
  declaration->diagnostic_token = current_token();
  if (current_token()->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, current_token(),
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  tk_set_current_token(current_token()->next);
  tk_expect('(');
  declaration->condition = psx_expr_assign_in_contexts(
      semantic_context, local_registry, local_declarations);
  tk_expect(',');
  if (current_token()->kind != TK_STRING) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING,
                   current_token(), "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
  }
  tk_set_current_token(current_token()->next);
  tk_expect(')');
  tk_expect(';');
}
