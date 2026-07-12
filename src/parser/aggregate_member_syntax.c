#include "aggregate_member_syntax.h"

#include "anon_tag.h"
#include "declarator_syntax.h"
#include "diag.h"
#include "dynarray.h"
#include "enum_const.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../pragma_pack.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static token_t *find_aggregate_expression_end(
    token_t *token, token_kind_t first_end, token_kind_t second_end) {
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  for (token_t *current = token; current; current = current->next) {
    if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
        (current->kind == first_end || current->kind == second_end))
      return current;
    if (current->kind == TK_LPAREN) paren_depth++;
    else if (current->kind == TK_RPAREN) paren_depth--;
    else if (current->kind == TK_LBRACKET) bracket_depth++;
    else if (current->kind == TK_RBRACKET) bracket_depth--;
    else if (current->kind == TK_LBRACE) brace_depth++;
    else if (current->kind == TK_RBRACE) brace_depth--;
  }
  return NULL;
}

static void consume_aggregate_alignas(
    void *context, psx_type_spec_result_t *result) {
  (void)result;
  psx_parsed_aggregate_member_specifier_t *specifier = context;
  if (!specifier || specifier->alignas_expression_count >= 8) {
    psx_diag_ctx(current_token(), "aggregate-syntax",
                 "aggregate alignas limit exceeded");
  }
  tk_set_current_token(current_token()->next);
  tk_expect('(');
  token_t *start = current_token();
  token_t *end = find_aggregate_expression_end(
      start, TK_RPAREN, TK_EOF);
  if (!end) {
    psx_diag_ctx(current_token(), "aggregate-syntax",
                 "unterminated aggregate alignas");
  }
  specifier->alignas_expressions[specifier->alignas_expression_count++] =
      (psx_parsed_aggregate_const_expr_t){.start = start, .end = end};
  tk_set_current_token(end);
  tk_expect(')');
}

static void diagnose_member_declarator_too_complex(
    void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "member", "member declarator is too complex");
}

static int append_member_declarator_pointer(
    void *context, int is_const, int is_volatile, int nesting_depth) {
  (void)nesting_depth;
  psx_parsed_aggregate_member_declarator_t *declarator = context;
  return declarator && psx_declarator_shape_append_pointer(
                           &declarator->declarator_shape,
                           is_const, is_volatile);
}

static int consume_member_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count) {
  (void)nesting_depth;
  (void)direct_was_parenthesized;
  (void)direct_pointer_count;
  (void)frame_pointer_count;
  psx_parsed_aggregate_member_declarator_t *declarator = context;
  if (!declarator) return 0;
  if (current_token()->kind == TK_LBRACKET) {
    tk_expect('[');
    int has_size = current_token()->kind != TK_RBRACKET;
    token_t *expression_start = NULL;
    token_t *expression_end = NULL;
    if (has_size) {
      expression_start = current_token();
      expression_end = find_aggregate_expression_end(
          current_token(), TK_RBRACKET, TK_EOF);
      if (!expression_end) {
        psx_diag_ctx(current_token(), "aggregate-syntax",
                     "unterminated aggregate array bound");
      }
      tk_set_current_token(expression_end);
    }
    tk_expect(']');
    int op_index = declarator->declarator_shape.count;
    if (!psx_declarator_shape_append_array_ex(
            &declarator->declarator_shape,
            0, !has_size)) {
      diagnose_member_declarator_too_complex(context, current_token());
    }
    if (has_size) {
      if (declarator->array_bound_count >= 24) {
        diagnose_member_declarator_too_complex(context, current_token());
      }
      declarator->array_bounds[declarator->array_bound_count++] =
          (psx_parsed_aggregate_array_bound_t){
              .declarator_op_index = op_index,
              .expression = {
                  .start = expression_start,
                  .end = expression_end,
              },
          };
    }
    return 1;
  }
  if (current_token()->kind != TK_LPAREN) return 0;
  psx_funcptr_signature_t suffix = {0};
  psx_skip_func_param_list(&suffix);
  psx_decl_funcptr_sig_t function = {0};
  function.function.callable.signature = suffix;
  if (!psx_declarator_shape_append_function(
          &declarator->declarator_shape, function)) {
    diagnose_member_declarator_too_complex(context, current_token());
  }
  return 1;
}

psx_parsed_aggregate_member_declarator_t
psx_parse_aggregate_member_declarator(void) {
  psx_parsed_aggregate_member_declarator_t declarator = {0};
  psx_declarator_shape_init(&declarator.declarator_shape);
  declarator.member = psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &declarator,
          .consume_suffix = consume_member_declarator_suffix,
          .append_pointer = append_member_declarator_pointer,
          .diagnose_too_complex = diagnose_member_declarator_too_complex,
      },
      &declarator.pointer_levels);
  if (tk_consume(':')) {
    declarator.has_bitfield = 1;
    declarator.bit_width_expression.start = current_token();
    declarator.bit_width_expression.end = find_aggregate_expression_end(
        current_token(), TK_COMMA, TK_SEMI);
    if (!declarator.bit_width_expression.end) {
      psx_diag_ctx(current_token(), "aggregate-syntax",
                   "unterminated aggregate bit-field width");
    }
    tk_set_current_token(declarator.bit_width_expression.end);
  }
  declarator.diagnostic_token = declarator.member
                                    ? (token_t *)declarator.member
                                    : current_token();
  return declarator;
}

static void parse_member_tag_specifier(
    psx_parsed_aggregate_member_specifier_t *specifier) {
  psx_decl_type_request_t *declaration = &specifier->declaration;
  psx_parsed_member_tag_action_t *action = &specifier->tag_action;
  action->diagnostic_token = current_token();
  declaration->tag_kind = current_token()->kind;
  action->kind = declaration->tag_kind;
  tk_set_current_token(current_token()->next);
  token_ident_t *tag = tk_consume_ident();
  if (tag) {
    declaration->tag_name = tag->str;
    declaration->tag_len = tag->len;
  } else if (current_token()->kind == TK_LBRACE) {
    psx_make_anonymous_tag_name(
        &declaration->tag_name, &declaration->tag_len);
  } else {
    psx_diag_missing(current_token(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }
  action->name = declaration->tag_name;
  action->name_len = declaration->tag_len;

  if (tk_consume('{')) {
    action->action = PSX_PARSED_MEMBER_TAG_DEFINITION;
    if (declaration->tag_kind == TK_ENUM) {
      action->enum_body = calloc(1, sizeof(*action->enum_body));
      if (!action->enum_body) {
        psx_diag_ctx(current_token(), "aggregate-syntax",
                     "enum body allocation failed");
      }
      psx_parse_enum_body(action->enum_body);
    } else {
      action->aggregate_body = calloc(1, sizeof(*action->aggregate_body));
      if (!action->aggregate_body) {
        psx_diag_ctx(current_token(), "aggregate-syntax",
                     "nested aggregate body allocation failed");
      }
      psx_parse_aggregate_body(action->aggregate_body);
    }
  } else {
    action->action = PSX_PARSED_MEMBER_TAG_REFERENCE;
  }
}

void psx_parse_aggregate_member_specifier(
    psx_parsed_aggregate_member_specifier_t *specifier) {
  if (!specifier) return;
  memset(specifier, 0, sizeof(*specifier));
  specifier->declaration.base_kind = TK_EOF;
  specifier->declaration.tag_kind = TK_EOF;
  specifier->declaration.fp_kind = TK_FLOAT_KIND_NONE;
  specifier->declaration.elem_size = 8;

  psx_type_spec_result_t type_spec = {0};
  token_kind_t builtin_kind = psx_consume_type_kind_with_syntax_ex(
      &type_spec,
      &(psx_type_spec_syntax_t){
          .context = specifier,
          .consume_alignas = consume_aggregate_alignas,
      });
  specifier->declaration.is_unsigned = type_spec.is_unsigned;
  specifier->declaration.is_complex = type_spec.is_complex;
  specifier->declaration.is_const_qualified = type_spec.is_const_qualified;
  specifier->declaration.is_volatile_qualified = type_spec.is_volatile_qualified;
  specifier->declaration.is_atomic = type_spec.is_atomic;
  specifier->declaration.is_long_long = type_spec.is_long_long;
  specifier->declaration.is_plain_char = type_spec.is_plain_char;
  specifier->declaration.is_long_double = type_spec.is_long_double;

  if (builtin_kind != TK_EOF) {
    specifier->declaration.base_kind = builtin_kind;
    specifier->declaration.override_plain_char = builtin_kind == TK_CHAR;
    psx_ctx_get_type_info(
        builtin_kind, NULL, &specifier->declaration.elem_size);
    if (builtin_kind == TK_FLOAT)
      specifier->declaration.fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (builtin_kind == TK_DOUBLE)
      specifier->declaration.fp_kind = TK_FLOAT_KIND_DOUBLE;
    if (type_spec.is_complex) specifier->declaration.elem_size *= 2;
    return;
  }

  if (psx_ctx_is_tag_keyword(current_token()->kind)) {
    parse_member_tag_specifier(specifier);
    return;
  }
  if (psx_ctx_is_typedef_name_token(current_token())) {
    token_ident_t *typedef_name = (token_ident_t *)current_token();
    psx_ctx_find_typedef_decl_type(
        typedef_name->str, typedef_name->len,
        &specifier->declaration.base_decl_type);
    tk_set_current_token(current_token()->next);
    return;
  }
  psx_diag_ctx(current_token(), "decl", "%s",
               diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
}

static psx_parsed_aggregate_item_t *append_aggregate_item(
    psx_parsed_aggregate_body_t *body) {
  if (body->item_count >= PS_MAX_DECLARATOR_COUNT) {
    psx_diag_ctx(current_token(), "aggregate-syntax",
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

static void append_aggregate_declarator(
    psx_parsed_aggregate_member_declaration_t *declaration,
    psx_parsed_aggregate_member_declarator_t declarator) {
  if (declaration->declarator_count >= PS_MAX_DECLARATOR_COUNT) {
    psx_diag_ctx(current_token(), "aggregate-syntax",
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
  declaration->declarators[declaration->declarator_count++] = declarator;
}

void psx_parse_aggregate_body(psx_parsed_aggregate_body_t *body) {
  if (!body) return;
  memset(body, 0, sizeof(*body));
  while (!tk_consume('}')) {
    psx_parsed_aggregate_item_t *item = append_aggregate_item(body);
    if (current_token()->kind == TK_STATIC_ASSERT) {
      item->kind = PSX_PARSED_AGGREGATE_STATIC_ASSERT;
      psx_parse_static_assert_syntax(&item->value.static_assertion);
      continue;
    }

    item->kind = PSX_PARSED_AGGREGATE_MEMBER_DECLARATION;
    psx_parsed_aggregate_member_declaration_t *declaration =
        &item->value.member_declaration;
    psx_parse_aggregate_member_specifier(&declaration->specifier);
    declaration->pack_alignment = pragma_pack_current_alignment();
    for (;;) {
      psx_parsed_aggregate_member_declarator_t declarator =
          psx_parse_aggregate_member_declarator();
      append_aggregate_declarator(declaration, declarator);
      int has_comma = tk_consume(',');
      if (!declarator.member && !declarator.has_bitfield && has_comma)
        psx_diag_missing(current_token(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!has_comma) break;
    }
    tk_expect(';');
  }
}

void psx_dispose_parsed_aggregate_body(psx_parsed_aggregate_body_t *body) {
  if (!body) return;
  for (int i = 0; i < body->item_count; i++) {
    if (body->items[i].kind == PSX_PARSED_AGGREGATE_MEMBER_DECLARATION) {
      psx_parsed_member_tag_action_t *tag_action =
          &body->items[i].value.member_declaration.specifier.tag_action;
      if (tag_action->aggregate_body) {
        psx_dispose_parsed_aggregate_body(tag_action->aggregate_body);
        free(tag_action->aggregate_body);
      }
      if (tag_action->enum_body) {
        psx_dispose_parsed_enum_body(tag_action->enum_body);
        free(tag_action->enum_body);
      }
      free(body->items[i].value.member_declaration.declarators);
    }
  }
  free(body->items);
  memset(body, 0, sizeof(*body));
}
