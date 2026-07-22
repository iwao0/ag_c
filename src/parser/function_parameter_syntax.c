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
    specifier_options.allow_implicit_int =
        type_mode == PSX_PARAMETER_TYPE_ALLOW_IMPLICIT_INT;
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
        parameter->specifier.type_spec.is_noreturn) {
      ps_diag_ctx_in(
          diagnostics(runtime_context),
          parameter->specifier.diagnostic_token,
          "function-parameters",
          "parameter declaration cannot use alignment or function specifiers");
    }
    parameter->declarator =
        psx_parse_parameter_declarator_syntax_tree_in_contexts(
            options);
    if (tk_consume_ctx(tk_ctx, ',')) continue;
    tk_expect_ctx(tk_ctx, ')');
    return 1;
  }
}

void psx_dispose_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters) {
  if (!parameters) return;
  for (int i = 0; i < parameters->count; i++) {
    ps_dispose_decl_specifier_syntax(&parameters->items[i].specifier);
    psx_dispose_declarator_syntax(&parameters->items[i].declarator);
  }
  free(parameters->items);
  memset(parameters, 0, sizeof(*parameters));
}
