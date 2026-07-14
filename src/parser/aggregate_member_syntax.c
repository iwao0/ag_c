#include "aggregate_member_syntax.h"

#include "diag.h"
#include "dynarray.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../pragma_pack.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static psx_parsed_aggregate_item_t *append_aggregate_item(
    psx_parsed_aggregate_body_t *body) {
  if (body->item_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx(current_token(), "aggregate-syntax",
                 "aggregate declaration limit exceeded");
  }
  if (body->item_count == body->item_capacity) {
    body->item_capacity = pda_next_cap(
        body->item_capacity, body->item_count + 1);
    body->items = pda_xreallocarray(
        body->items, (size_t)body->item_capacity, sizeof(*body->items));
  }
  psx_parsed_aggregate_item_t *item = &body->items[body->item_count++];
  memset(item, 0, sizeof(*item));
  return item;
}

static psx_parsed_declarator_t *append_aggregate_declarator(
    psx_parsed_aggregate_member_declaration_t *declaration) {
  if (declaration->declarator_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx(current_token(), "aggregate-syntax",
                 "aggregate declarator limit exceeded");
  }
  if (declaration->declarator_count == declaration->declarator_capacity) {
    declaration->declarator_capacity = pda_next_cap(
        declaration->declarator_capacity,
        declaration->declarator_count + 1);
    declaration->declarators = pda_xreallocarray(
        declaration->declarators,
        (size_t)declaration->declarator_capacity,
        sizeof(*declaration->declarators));
  }
  psx_parsed_declarator_t *declarator =
      &declaration->declarators[declaration->declarator_count++];
  memset(declarator, 0, sizeof(*declarator));
  return declarator;
}

void psx_parse_aggregate_body(psx_parsed_aggregate_body_t *body) {
  psx_semantic_context_t *semantic_context = ps_ctx_active();
  psx_parse_aggregate_body_with_options(
      body,
      &(psx_decl_specifier_syntax_options_t){
          .semantic_context = semantic_context,
      });
}

void psx_parse_aggregate_body_with_options(
    psx_parsed_aggregate_body_t *body,
    const psx_decl_specifier_syntax_options_t *options) {
  if (!body) return;
  memset(body, 0, sizeof(*body));
  while (!tk_consume('}')) {
    psx_parsed_aggregate_item_t *item = append_aggregate_item(body);
    if (current_token()->kind == TK_STATIC_ASSERT) {
      item->kind = PSX_PARSED_AGGREGATE_STATIC_ASSERT;
      psx_parse_static_assert_syntax_in_context(
          &item->value.static_assertion,
          options ? options->semantic_context : NULL, NULL);
      continue;
    }

    item->kind = PSX_PARSED_AGGREGATE_MEMBER_DECLARATION;
    psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    psx_parse_decl_specifier_syntax_ex(
        &declaration->specifier, options);
    declaration->pack_alignment = pragma_pack_current_alignment();
    for (;;) {
      psx_parsed_declarator_t *declarator =
          append_aggregate_declarator(declaration);
      psx_parse_declarator_syntax_tree_into_with_typedef_lookup(
          declarator,
          options ? options->is_typedef_name : NULL,
          options ? options->context : NULL);
      int has_comma = tk_consume(',');
      if (!declarator->identifier && !declarator->has_bitfield && has_comma)
        ps_diag_missing(current_token(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!has_comma) break;
    }
    tk_expect(';');
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
