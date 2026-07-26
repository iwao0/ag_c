#include "function_parameter_syntax.h"

#include "diag.h"
#include "dynarray.h"
#include "runtime_context.h"
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

static psx_parsed_function_parameter_t *append_function_parameter(
    psx_parsed_function_parameters_t *parameters,
    psx_parser_runtime_context_t *runtime_context) {
  if (parameters->count == parameters->capacity) {
    parameters->capacity = pda_next_cap_in(
        diagnostics(runtime_context), parameters->capacity,
        parameters->count + 1);
    parameters->items = pda_xreallocarray_in(
        diagnostics(runtime_context), parameters->items,
        (size_t)parameters->capacity,
        sizeof(*parameters->items));
  }
  psx_parsed_function_parameter_t *parameter =
      &parameters->items[parameters->count++];
  memset(parameter, 0, sizeof(*parameter));
  return parameter;
}

static int identifier_names_equal(
    const token_ident_t *left, const token_ident_t *right) {
  return left && right && left->len == right->len &&
         memcmp(left->str, right->str, (size_t)left->len) == 0;
}

static int parameter_list_contains_identifier(
    const psx_parsed_function_parameters_t *parameters,
    const token_ident_t *identifier) {
  for (int i = 0; parameters && i < parameters->count; i++) {
    if (identifier_names_equal(
            parameters->items[i].declarator.identifier, identifier))
      return 1;
  }
  return 0;
}

static int token_starts_identifier_list(
    psx_function_parameter_type_mode_t type_mode,
    const psx_decl_specifier_syntax_options_t *options,
    token_t *token) {
  return type_mode == PSX_PARAMETER_TYPE_ALLOW_IDENTIFIER_LIST &&
         token && token->kind == TK_IDENT &&
         (!options->name_classifier ||
          !ps_name_classifier_is_typedef_name(
              options->name_classifier, token));
}

static int parse_identifier_list(
    psx_parsed_function_parameters_t *parameters,
    psx_parser_runtime_context_t *runtime_context) {
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  parameters->is_identifier_list = 1;
  for (;;) {
    token_ident_t *identifier = tk_consume_ident_ctx(tk_ctx);
    if (!identifier) {
      ps_diag_ctx_in(
          diagnostics(runtime_context), current_token(runtime_context),
          "function-parameters",
          "old-style function identifier list requires a parameter name");
      return 0;
    }
    if (parameter_list_contains_identifier(parameters, identifier)) {
      ps_diag_ctx_in(
          diagnostics(runtime_context), (token_t *)identifier,
          "function-parameters",
          "duplicate parameter name '%.*s' in old-style function "
          "identifier list",
          identifier->len, identifier->str);
      return 0;
    }
    psx_parsed_function_parameter_t *parameter =
        append_function_parameter(parameters, runtime_context);
    parameter->declarator.identifier = identifier;
    parameter->declarator.diagnostic_token =
        (token_t *)identifier;
    if (tk_consume_ctx(tk_ctx, ',')) continue;
    tk_expect_ctx(tk_ctx, ')');
    return 1;
  }
}

static void synchronize_function_parameters(
    psx_parser_runtime_context_t *runtime_context) {
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  int depth = 0;
  while (current_token(runtime_context)->kind != TK_EOF) {
    token_kind_t kind = current_token(runtime_context)->kind;
    tk_ensure_lookahead_ctx(tk_ctx);
    if (current_token(runtime_context)->next)
      tk_set_current_token_ctx(
          tk_ctx, current_token(runtime_context)->next);
    if (kind == TK_LPAREN) {
      depth++;
    } else if (kind == TK_RPAREN) {
      if (depth == 0) return;
      depth--;
    }
  }
}

int psx_parse_function_parameters_syntax_with_typedef_lookup_in_contexts(
    psx_parsed_function_parameters_t *parameters,
    psx_function_parameter_type_mode_t type_mode,
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parser_runtime_context_t *runtime_context =
      options ? options->runtime_context : NULL;
  if (!parameters || !options || !runtime_context ||
      !tokenizer(runtime_context))
    return 0;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  tk_expect_ctx(tk_ctx, '(');
  if (tk_consume_ctx(tk_ctx, ')')) return 1;
  if (token_starts_identifier_list(
          type_mode, options, current_token(runtime_context)))
    return parse_identifier_list(parameters, runtime_context);
  for (;;) {
    if (current_token(runtime_context)->kind == TK_ELLIPSIS) {
      if (parameters->count == 0) {
        ps_diag_ctx_in(
            diagnostics(runtime_context), current_token(runtime_context),
            "function-parameters",
            "ISO C requires a named parameter before '...'");
      }
      tk_set_current_token_ctx(
          tk_ctx, current_token(runtime_context)->next);
      parameters->is_variadic = 1;
      tk_expect_ctx(tk_ctx, ')');
      return 1;
    }
    psx_parsed_function_parameter_t *parameter =
        append_function_parameter(parameters, runtime_context);
    psx_decl_specifier_syntax_options_t specifier_options = *options;
    specifier_options.name_classifier =
        type_mode == PSX_PARAMETER_TYPE_DEFERRED_TYPEDEF
            ? NULL : options->name_classifier;
    specifier_options.allow_implicit_int = 0;
    int parsed_specifier = psx_try_parse_decl_specifier_syntax_ex(
        &parameter->specifier, &specifier_options);
    if (!parsed_specifier) {
      diag_report_tokf_in(
          diagnostics(runtime_context), DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN,
          current_token(runtime_context), "%s",
          diag_message_for_in(diagnostics(runtime_context),
                              DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
      synchronize_function_parameters(runtime_context);
      return 0;
    }
    if (parameter->specifier.alignas_specifier_count > 0 ||
        parameter->specifier.type_spec.is_inline ||
        parameter->specifier.type_spec.is_noreturn ||
        parameter->specifier.type_spec.is_typedef ||
        parameter->specifier.type_spec.is_extern ||
        parameter->specifier.type_spec.is_static ||
        parameter->specifier.type_spec.is_auto ||
        parameter->specifier.type_spec.is_thread_local) {
      ps_diag_ctx_in(
          diagnostics(runtime_context),
          parameter->specifier.diagnostic_token,
          "function-parameters",
          "parameter declaration may only use the 'register' storage class");
    }
    parameter->declarator =
        psx_parse_parameter_declarator_syntax_tree_in_contexts(
            options);
    if (tk_consume_ctx(tk_ctx, ',')) continue;
    tk_expect_ctx(tk_ctx, ')');
    return 1;
  }
}

static psx_parsed_old_style_parameter_declaration_t *
append_old_style_declaration(
    psx_parsed_function_parameters_t *parameters,
    psx_parser_runtime_context_t *runtime_context) {
  if (parameters->old_style_declaration_count ==
      parameters->old_style_declaration_capacity) {
    parameters->old_style_declaration_capacity =
        pda_next_cap_in(
            diagnostics(runtime_context),
            parameters->old_style_declaration_capacity,
            parameters->old_style_declaration_count + 1);
    parameters->old_style_declarations =
        pda_xreallocarray_in(
            diagnostics(runtime_context),
            parameters->old_style_declarations,
            (size_t)parameters->old_style_declaration_capacity,
            sizeof(*parameters->old_style_declarations));
  }
  psx_parsed_old_style_parameter_declaration_t *declaration =
      &parameters->old_style_declarations[
          parameters->old_style_declaration_count++];
  memset(declaration, 0, sizeof(*declaration));
  declaration->diagnostic_token =
      current_token(runtime_context);
  return declaration;
}

static psx_parsed_declarator_t *append_old_style_declarator(
    psx_parsed_old_style_parameter_declaration_t *declaration,
    psx_parser_runtime_context_t *runtime_context) {
  if (declaration->declarator_count ==
      declaration->declarator_capacity) {
    declaration->declarator_capacity = pda_next_cap_in(
        diagnostics(runtime_context),
        declaration->declarator_capacity,
        declaration->declarator_count + 1);
    declaration->declarators = pda_xreallocarray_in(
        diagnostics(runtime_context), declaration->declarators,
        (size_t)declaration->declarator_capacity,
        sizeof(*declaration->declarators));
  }
  psx_parsed_declarator_t *declarator =
      &declaration->declarators[declaration->declarator_count++];
  memset(declarator, 0, sizeof(*declarator));
  return declarator;
}

static int old_style_declarator_count_for_identifier(
    const psx_parsed_function_parameters_t *parameters,
    const token_ident_t *identifier) {
  int count = 0;
  for (int d = 0;
       parameters && d < parameters->old_style_declaration_count;
       d++) {
    const psx_parsed_old_style_parameter_declaration_t *declaration =
        &parameters->old_style_declarations[d];
    for (int i = 0; i < declaration->declarator_count; i++) {
      if (identifier_names_equal(
              declaration->declarators[i].identifier, identifier))
        count++;
    }
  }
  return count;
}

static int validate_old_style_parameter_declaration_specifier(
    const psx_parsed_decl_specifier_t *specifier,
    psx_parser_runtime_context_t *runtime_context) {
  if (!specifier) return 0;
  if (specifier->alignas_specifier_count > 0 ||
      specifier->type_spec.is_inline ||
      specifier->type_spec.is_noreturn ||
      specifier->type_spec.is_typedef ||
      specifier->type_spec.is_extern ||
      specifier->type_spec.is_static ||
      specifier->type_spec.is_auto ||
      specifier->type_spec.is_thread_local) {
    ps_diag_ctx_in(
        diagnostics(runtime_context),
        specifier->diagnostic_token,
        "function-parameters",
        "old-style parameter declaration may only use the "
        "'register' storage class");
    return 0;
  }
  return 1;
}

int psx_parse_old_style_function_parameter_declarations_syntax_in_contexts(
    psx_parsed_function_parameters_t *parameters,
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parser_runtime_context_t *runtime_context =
      options ? options->runtime_context : NULL;
  if (!parameters || !parameters->is_identifier_list ||
      !options || !runtime_context || !tokenizer(runtime_context))
    return 0;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  while (current_token(runtime_context)->kind != TK_LBRACE &&
         current_token(runtime_context)->kind != TK_SEMI &&
         current_token(runtime_context)->kind != TK_EOF) {
    psx_parsed_old_style_parameter_declaration_t *declaration =
        append_old_style_declaration(parameters, runtime_context);
    psx_decl_specifier_syntax_options_t specifier_options = *options;
    specifier_options.allow_implicit_int = 0;
    if (!psx_try_parse_decl_specifier_syntax_ex(
            &declaration->specifier, &specifier_options)) {
      diag_report_tokf_in(
          diagnostics(runtime_context),
          DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN,
          current_token(runtime_context), "%s",
          diag_message_for_in(
              diagnostics(runtime_context),
              DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
      return 0;
    }
    if (!validate_old_style_parameter_declaration_specifier(
            &declaration->specifier, runtime_context))
      return 0;
    for (;;) {
      psx_parsed_declarator_t *declarator =
          append_old_style_declarator(
              declaration, runtime_context);
      *declarator =
          psx_parse_parameter_declarator_syntax_tree_in_contexts(
              options);
      token_ident_t *identifier = declarator->identifier;
      if (!identifier) {
        ps_diag_ctx_in(
            diagnostics(runtime_context),
            declarator->diagnostic_token,
            "function-parameters",
            "old-style parameter declaration requires a name");
        return 0;
      }
      if (!parameter_list_contains_identifier(
              parameters, identifier)) {
        ps_diag_ctx_in(
            diagnostics(runtime_context), (token_t *)identifier,
            "function-parameters",
            "declaration for '%.*s' does not match an identifier "
            "in the old-style function parameter list",
            identifier->len, identifier->str);
        return 0;
      }
      if (old_style_declarator_count_for_identifier(
              parameters, identifier) != 1) {
        ps_diag_ctx_in(
            diagnostics(runtime_context), (token_t *)identifier,
            "function-parameters",
            "old-style function parameter '%.*s' is declared "
            "more than once",
            identifier->len, identifier->str);
        return 0;
      }
      if (current_token(runtime_context)->kind == TK_ASSIGN) {
        ps_diag_ctx_in(
            diagnostics(runtime_context),
            current_token(runtime_context),
            "function-parameters",
            "old-style function parameter cannot have an initializer");
        return 0;
      }
      if (!tk_consume_ctx(tk_ctx, ',')) break;
    }
    tk_expect_ctx(tk_ctx, ';');
  }
  if (current_token(runtime_context)->kind != TK_LBRACE)
    return 1;
  for (int i = 0; i < parameters->count; i++) {
    token_ident_t *identifier =
        parameters->items[i].declarator.identifier;
    if (old_style_declarator_count_for_identifier(
            parameters, identifier) == 1)
      continue;
    diag_report_tokf_in(
        diagnostics(runtime_context),
        DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN,
        (token_t *)identifier,
        "old-style function parameter '%.*s' requires an explicit "
        "declaration before the function body",
        identifier->len, identifier->str);
    return 0;
  }
  return 1;
}

int psx_function_definition_parameter_syntax_at(
    const psx_parsed_function_parameters_t *parameters, int index,
    psx_parsed_function_parameter_t *parameter) {
  if (parameter)
    *parameter = (psx_parsed_function_parameter_t){0};
  if (!parameters || !parameter || index < 0 ||
      index >= parameters->count)
    return 0;
  if (!parameters->is_identifier_list) {
    *parameter = parameters->items[index];
    return 1;
  }
  token_ident_t *identifier =
      parameters->items[index].declarator.identifier;
  for (int d = 0; d < parameters->old_style_declaration_count; d++) {
    const psx_parsed_old_style_parameter_declaration_t *declaration =
        &parameters->old_style_declarations[d];
    for (int i = 0; i < declaration->declarator_count; i++) {
      if (!identifier_names_equal(
              declaration->declarators[i].identifier, identifier))
        continue;
      parameter->specifier = declaration->specifier;
      parameter->declarator = declaration->declarators[i];
      parameter->shared_specifier = &declaration->specifier;
      return 1;
    }
  }
  return 0;
}

void psx_dispose_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters) {
  if (!parameters) return;
  for (int i = 0; i < parameters->count; i++) {
    ps_dispose_decl_specifier_syntax(&parameters->items[i].specifier);
    psx_dispose_declarator_syntax(&parameters->items[i].declarator);
  }
  for (int d = 0; d < parameters->old_style_declaration_count; d++) {
    psx_parsed_old_style_parameter_declaration_t *declaration =
        &parameters->old_style_declarations[d];
    ps_dispose_decl_specifier_syntax(&declaration->specifier);
    for (int i = 0; i < declaration->declarator_count; i++)
      psx_dispose_declarator_syntax(
          &declaration->declarators[i]);
    free(declaration->declarators);
  }
  free(parameters->items);
  free(parameters->old_style_declarations);
  memset(parameters, 0, sizeof(*parameters));
}
