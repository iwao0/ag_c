#include "aggregate_member_syntax.h"

#include "diag.h"
#include "dynarray.h"
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../pragma_pack.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static tokenizer_context_t *tokenizer(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_tokenizer(runtime_context);
}

static token_t *current_token(
    psx_parser_runtime_context_t *runtime_context) {
  return tk_get_current_token_ctx(tokenizer(runtime_context));
}

static ag_diagnostic_context_t *diagnostics(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_diagnostics(runtime_context);
}

static node_t *parse_aggregate_static_assert_assignment_expression(
    void *context) {
  const psx_decl_specifier_syntax_options_t *options = context;
  return options && options->parse_assignment_expression
             ? options->parse_assignment_expression(
                   options->expression_context)
             : NULL;
}

static psx_parsed_aggregate_item_t *append_aggregate_item(
    psx_parsed_aggregate_body_t *body,
    psx_parser_runtime_context_t *runtime_context) {
  if (body->item_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "aggregate-syntax",
                 "aggregate declaration limit exceeded");
  }
  if (body->item_count == body->item_capacity) {
    body->item_capacity = pda_next_cap_in(diagnostics(runtime_context),
        body->item_capacity, body->item_count + 1);
    body->items = pda_xreallocarray_in(diagnostics(runtime_context),
        body->items, (size_t)body->item_capacity, sizeof(*body->items));
  }
  psx_parsed_aggregate_item_t *item = &body->items[body->item_count++];
  memset(item, 0, sizeof(*item));
  return item;
}

static psx_parsed_declarator_t *append_aggregate_declarator(
    psx_parsed_aggregate_member_declaration_t *declaration,
    psx_parser_runtime_context_t *runtime_context) {
  if (declaration->declarator_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "aggregate-syntax",
                 "aggregate declarator limit exceeded");
  }
  if (declaration->declarator_count == declaration->declarator_capacity) {
    declaration->declarator_capacity = pda_next_cap_in(diagnostics(runtime_context),
        declaration->declarator_capacity,
        declaration->declarator_count + 1);
    declaration->declarators = pda_xreallocarray_in(diagnostics(runtime_context),
        declaration->declarators,
        (size_t)declaration->declarator_capacity,
        sizeof(*declaration->declarators));
  }
  psx_parsed_declarator_t *declarator =
      &declaration->declarators[declaration->declarator_count++];
  memset(declarator, 0, sizeof(*declarator));
  return declarator;
}

void psx_parse_aggregate_body_with_options(
    psx_parsed_aggregate_body_t *body,
    const psx_decl_specifier_syntax_options_t *options) {
  if (!body) return;
  if (!options || !options->semantic_context ||
      !options->runtime_context ||
      !tokenizer(options->runtime_context)) {
    return;
  }
  psx_parser_runtime_context_t *runtime_context = options->runtime_context;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  memset(body, 0, sizeof(*body));
  while (!tk_consume_ctx(tk_ctx, '}')) {
    psx_parsed_aggregate_item_t *item =
        append_aggregate_item(body, runtime_context);
    if (current_token(runtime_context)->kind == TK_STATIC_ASSERT) {
      item->kind = PSX_PARSED_AGGREGATE_STATIC_ASSERT;
      psx_parse_static_assert_syntax_with_context(
          &item->value.static_assertion,
          &(psx_static_assert_syntax_context_t){
              .context = (void *)options,
              .runtime_context = options->runtime_context,
              .parse_assignment_expression =
                  parse_aggregate_static_assert_assignment_expression,
          });
      continue;
    }

    item->kind = PSX_PARSED_AGGREGATE_MEMBER_DECLARATION;
    psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    psx_parse_decl_specifier_syntax_ex(
        &declaration->specifier, options);
    declaration->pack_alignment = pragma_pack_current_alignment_in(
        options->runtime_context);
    for (;;) {
      psx_parsed_declarator_t *declarator =
          append_aggregate_declarator(declaration, runtime_context);
      psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
          declarator, options);
      int has_comma = tk_consume_ctx(tk_ctx, ',');
      if (!declarator->identifier && !declarator->has_bitfield && has_comma)
        ps_diag_missing_in(diagnostics(runtime_context),
            current_token(runtime_context),
            diag_text_for_in(diagnostics(runtime_context), DIAG_TEXT_MEMBER_NAME));
      if (!has_comma) break;
    }
    tk_expect_ctx(tk_ctx, ';');
  }
}

void psx_dispose_parsed_aggregate_body(psx_parsed_aggregate_body_t *body) {
  if (!body) return;
  for (int i = 0; i < body->item_count; i++) {
    if (body->items[i].kind == PSX_PARSED_AGGREGATE_MEMBER_DECLARATION) {
      psx_parsed_aggregate_member_declaration_t *declaration =
          &body->items[i].value.member_declaration;
      ps_dispose_decl_specifier_syntax(&declaration->specifier);
      for (int j = 0; j < declaration->declarator_count; j++)
        psx_dispose_declarator_syntax(&declaration->declarators[j]);
      free(declaration->declarators);
    }
  }
  free(body->items);
  memset(body, 0, sizeof(*body));
}
