#include "static_assert_declaration.h"

#include "expr.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../semantic/constant_expression.h"
#include "../semantic/static_assert_resolution.h"
#include "../tokenizer/tokenizer.h"

static token_t *current_token(void) { return tk_get_current_token(); }

void psx_parse_static_assert_declaration(void) {
  if (current_token()->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, current_token(),
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  tk_set_current_token(current_token()->next);
  tk_expect('(');
  int is_constant = 1;
  long long value = psx_eval_const_int(psx_expr_assign(), &is_constant);
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

  psx_static_assert_resolution_t resolution;
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){
          .is_constant = is_constant,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_STATIC_ASSERT_NOT_CONSTANT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST,
                   current_token(), "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
  }
  if (resolution.status == PSX_STATIC_ASSERT_FAILED) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, current_token(),
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
  }
}
