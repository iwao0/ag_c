#include "enum_const.h"

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "dynarray.h"
#include "core.h"
#include "../tokenizer/tokenizer.h"

void psx_parse_enum_body_syntax(
    psx_parsed_enum_body_t *body,
    const psx_enum_body_syntax_context_t *context) {
  if (!body || !context || !context->diagnostics ||
      !context->parse_assignment_expression ||
      !context->tokenizer_context)
    return;
  tokenizer_context_t *tokenizer_context = context->tokenizer_context;
  memset(body, 0, sizeof(*body));
  if (tk_get_current_token_ctx(tokenizer_context)->kind ==
      TK_RBRACE) {
    ps_diag_ctx_in(
        context->diagnostics,
        tk_get_current_token_ctx(tokenizer_context), "enum-syntax",
        "an enumeration requires at least one enumerator");
  }
  while (!tk_consume_ctx(tokenizer_context, '}')) {
    if (body->member_count >= PS_MAX_DECLARATOR_COUNT) {
      ps_diag_ctx_in(
          context->diagnostics,
          tk_get_current_token_ctx(tokenizer_context), "enum-syntax",
          "enum member limit exceeded");
    }
    if (body->member_count == body->member_capacity) {
      body->member_capacity = pda_next_cap_in(
          context->diagnostics, body->member_capacity,
          body->member_count + 1);
      body->members = pda_xreallocarray_in(
          context->diagnostics, body->members,
          (size_t)body->member_capacity, sizeof(*body->members));
    }
    psx_parsed_enum_member_t *member =
        &body->members[body->member_count++];
    memset(member, 0, sizeof(*member));
    member->enumerator = tk_consume_ident_ctx(tokenizer_context);
    if (!member->enumerator) {
      ps_diag_missing_in(
          context->diagnostics,
          tk_get_current_token_ctx(tokenizer_context),
          diag_text_for_in(
              context->diagnostics, DIAG_TEXT_ENUMERATOR_NAME));
    }
    ps_name_classifier_declare(
        context->name_classifier, (token_t *)member->enumerator, 0);
    if (tk_consume_ctx(tokenizer_context, '='))
      member->initializer = context->parse_assignment_expression(
          context->expression_context);
    if (tk_consume_ctx(tokenizer_context, '}')) break;
    tk_expect_ctx(tokenizer_context, ',');
    if (tk_consume_ctx(tokenizer_context, '}')) break;
  }
}

void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body) {
  if (!body) return;
  free(body->members);
  memset(body, 0, sizeof(*body));
}
