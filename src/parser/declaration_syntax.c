#include "declaration_syntax.h"

#include "aggregate_member_syntax.h"
#include "anon_tag.h"
#include "declarator_syntax.h"
#include "diag.h"
#include "function_parameter_syntax.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static token_t *find_declaration_expression_end(
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

static void consume_declaration_alignas(
    void *context, psx_type_spec_result_t *result) {
  (void)result;
  psx_parsed_decl_specifier_t *specifier = context;
  if (!specifier || specifier->alignas_expression_count >= 8) {
    psx_diag_ctx(current_token(), "declaration-syntax",
                 "declaration alignas limit exceeded");
  }
  tk_set_current_token(current_token()->next);
  tk_expect('(');
  token_t *start = current_token();
  token_t *end = find_declaration_expression_end(
      start, TK_RPAREN, TK_EOF);
  if (!end) {
    psx_diag_ctx(current_token(), "declaration-syntax",
                 "unterminated declaration alignas");
  }
  specifier->alignas_expressions[specifier->alignas_expression_count++] =
      (psx_parsed_const_expr_t){.start = start, .end = end};
  tk_set_current_token(end);
  tk_expect(')');
}

static void diagnose_declarator_too_complex(void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "declaration-syntax", "declarator is too complex");
}

static int append_declarator_pointer(
    void *context, int is_const, int is_volatile, int nesting_depth) {
  (void)nesting_depth;
  psx_parsed_declarator_t *declarator = context;
  return declarator && psx_declarator_shape_append_pointer(
                           &declarator->declarator_shape,
                           is_const, is_volatile);
}

static int consume_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count) {
  (void)nesting_depth;
  (void)direct_was_parenthesized;
  (void)direct_pointer_count;
  (void)frame_pointer_count;
  psx_parsed_declarator_t *declarator = context;
  if (!declarator) return 0;
  if (current_token()->kind == TK_LBRACKET) {
    tk_expect('[');
    int has_size = current_token()->kind != TK_RBRACKET;
    token_t *expression_start = NULL;
    token_t *expression_end = NULL;
    if (has_size) {
      expression_start = current_token();
      expression_end = find_declaration_expression_end(
          current_token(), TK_RBRACKET, TK_EOF);
      if (!expression_end) {
        psx_diag_ctx(current_token(), "declaration-syntax",
                     "unterminated array bound");
      }
      tk_set_current_token(expression_end);
    }
    tk_expect(']');
    int op_index = declarator->declarator_shape.count;
    if (!psx_declarator_shape_append_array_ex(
            &declarator->declarator_shape, 0, !has_size)) {
      diagnose_declarator_too_complex(context, current_token());
    }
    if (has_size) {
      if (declarator->array_bound_count >= 24)
        diagnose_declarator_too_complex(context, current_token());
      declarator->array_bounds[declarator->array_bound_count++] =
          (psx_parsed_array_bound_t){
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
  int op_index = declarator->declarator_shape.count;
  if (!psx_declarator_shape_append_function(
          &declarator->declarator_shape, (psx_decl_funcptr_sig_t){0})) {
    diagnose_declarator_too_complex(context, current_token());
  }
  if (declarator->function_suffix_count >= 24)
    diagnose_declarator_too_complex(context, current_token());
  psx_parsed_function_parameters_t *parameters =
      calloc(1, sizeof(*parameters));
  if (!parameters) {
    psx_diag_ctx(current_token(), "declaration-syntax",
                 "function parameter syntax allocation failed");
  }
  psx_parse_function_parameters_syntax(parameters);
  declarator->function_suffixes[declarator->function_suffix_count++] =
      (psx_parsed_function_suffix_t){
          .declarator_op_index = op_index,
          .parameters = parameters,
      };
  return 1;
}

psx_parsed_declarator_t psx_parse_declarator_syntax_tree(void) {
  psx_parsed_declarator_t declarator = {0};
  psx_declarator_shape_init(&declarator.declarator_shape);
  declarator.identifier = psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &declarator,
          .consume_suffix = consume_declarator_suffix,
          .append_pointer = append_declarator_pointer,
          .diagnose_too_complex = diagnose_declarator_too_complex,
      },
      &declarator.pointer_levels);
  if (tk_consume(':')) {
    declarator.has_bitfield = 1;
    declarator.bit_width_expression.start = current_token();
    declarator.bit_width_expression.end = find_declaration_expression_end(
        current_token(), TK_COMMA, TK_SEMI);
    if (!declarator.bit_width_expression.end) {
      psx_diag_ctx(current_token(), "declaration-syntax",
                   "unterminated bit-field width");
    }
    tk_set_current_token(declarator.bit_width_expression.end);
  }
  declarator.diagnostic_token = declarator.identifier
                                    ? (token_t *)declarator.identifier
                                    : current_token();
  return declarator;
}

static void parse_tag_specifier(psx_parsed_decl_specifier_t *specifier) {
  psx_parsed_tag_action_t *action = &specifier->tag_action;
  action->diagnostic_token = current_token();
  action->kind = current_token()->kind;
  tk_set_current_token(current_token()->next);
  token_ident_t *tag = tk_consume_ident();
  if (tag) {
    action->name = tag->str;
    action->name_len = tag->len;
  } else if (current_token()->kind == TK_LBRACE) {
    psx_make_anonymous_tag_name(&action->name, &action->name_len);
  } else {
    psx_diag_missing(current_token(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }

  if (tk_consume('{')) {
    action->action = PSX_PARSED_TAG_DEFINITION;
    if (action->kind == TK_ENUM) {
      action->enum_body = calloc(1, sizeof(*action->enum_body));
      if (!action->enum_body) {
        psx_diag_ctx(current_token(), "declaration-syntax",
                     "enum body allocation failed");
      }
      psx_parse_enum_body(action->enum_body);
    } else {
      action->aggregate_body = calloc(1, sizeof(*action->aggregate_body));
      if (!action->aggregate_body) {
        psx_diag_ctx(current_token(), "declaration-syntax",
                     "aggregate body allocation failed");
      }
      psx_parse_aggregate_body(action->aggregate_body);
    }
  } else {
    action->action = PSX_PARSED_TAG_REFERENCE;
  }
}

void psx_parse_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return;
  memset(specifier, 0, sizeof(*specifier));
  specifier->diagnostic_token = current_token();

  token_kind_t builtin_kind = psx_consume_type_kind_with_syntax_ex(
      &specifier->type_spec,
      &(psx_type_spec_syntax_t){
          .context = specifier,
          .consume_alignas = consume_declaration_alignas,
      });
  if (builtin_kind != TK_EOF) {
    specifier->source = PSX_PARSED_DECL_TYPE_BUILTIN;
    return;
  }
  if (current_token()->kind == TK_STRUCT ||
      current_token()->kind == TK_UNION ||
      current_token()->kind == TK_ENUM) {
    specifier->source = PSX_PARSED_DECL_TYPE_TAG;
    parse_tag_specifier(specifier);
    while (current_token()->kind == TK_CONST ||
           current_token()->kind == TK_VOLATILE) {
      if (current_token()->kind == TK_CONST)
        specifier->type_spec.is_const_qualified = 1;
      if (current_token()->kind == TK_VOLATILE)
        specifier->type_spec.is_volatile_qualified = 1;
      tk_set_current_token(current_token()->next);
    }
    return;
  }
  if (current_token()->kind == TK_IDENT) {
    specifier->source = PSX_PARSED_DECL_TYPEDEF_NAME;
    specifier->typedef_name = (token_ident_t *)current_token();
    tk_set_current_token(current_token()->next);
    return;
  }
  psx_diag_ctx(current_token(), "decl", "%s",
               diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
}

void psx_dispose_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return;
  psx_parsed_tag_action_t *tag_action = &specifier->tag_action;
  if (tag_action->aggregate_body) {
    psx_dispose_parsed_aggregate_body(tag_action->aggregate_body);
    free(tag_action->aggregate_body);
  }
  if (tag_action->enum_body) {
    psx_dispose_parsed_enum_body(tag_action->enum_body);
    free(tag_action->enum_body);
  }
}

void psx_dispose_declarator_syntax(psx_parsed_declarator_t *declarator) {
  if (!declarator) return;
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    psx_parsed_function_parameters_t *parameters =
        declarator->function_suffixes[i].parameters;
    if (!parameters) continue;
    psx_dispose_function_parameters_syntax(parameters);
    free(parameters);
  }
}
