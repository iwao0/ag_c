#include "function_parameter_syntax.h"

#include "diag.h"
#include "dynarray.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static psx_parsed_function_parameter_t *append_function_parameter(
    psx_parsed_function_parameters_t *parameters) {
  if (parameters->count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx(current_token(), "function-parameter-syntax",
                 "function parameter limit exceeded");
  }
  if (parameters->count == parameters->capacity) {
    parameters->capacity = pda_next_cap(
        parameters->capacity, parameters->count + 1);
    parameters->items = pda_xreallocarray(
        parameters->items, (size_t)parameters->capacity,
        sizeof(*parameters->items));
  }
  psx_parsed_function_parameter_t *parameter =
      &parameters->items[parameters->count++];
  memset(parameter, 0, sizeof(*parameter));
  return parameter;
}

static void synchronize_function_parameters(void) {
  int depth = 0;
  while (current_token()->kind != TK_EOF) {
    token_kind_t kind = current_token()->kind;
    tk_ensure_lookahead();
    if (current_token()->next)
      tk_set_current_token(current_token()->next);
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
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    psx_decl_typedef_name_predicate_t is_typedef_name,
    void *typedef_name_context) {
  if (!parameters || !semantic_context || !local_registry) return 0;
  tk_expect('(');
  if (tk_consume(')')) return 1;
  for (;;) {
    if (current_token()->kind == TK_ELLIPSIS) {
      tk_set_current_token(current_token()->next);
      parameters->is_variadic = 1;
      tk_expect(')');
      return 1;
    }
    psx_parsed_function_parameter_t *parameter =
        append_function_parameter(parameters);
    psx_decl_specifier_syntax_options_t specifier_options = {
        .is_typedef_name =
            type_mode == PSX_PARAMETER_TYPE_DEFERRED_TYPEDEF
                ? NULL : is_typedef_name,
        .context = typedef_name_context,
        .semantic_context = semantic_context,
        .local_registry = local_registry,
        .allow_implicit_int =
            type_mode == PSX_PARAMETER_TYPE_ALLOW_IMPLICIT_INT,
    };
    int parsed_specifier = psx_try_parse_decl_specifier_syntax_ex(
        &parameter->specifier, &specifier_options);
    if (!parsed_specifier) {
      diag_report_tokf(
          DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN, current_token(), "%s",
          diag_message_for(DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
      synchronize_function_parameters();
      return 0;
    }
    parameter->declarator =
        psx_parse_parameter_declarator_syntax_tree_in_contexts(
            semantic_context, local_registry,
            is_typedef_name, typedef_name_context);
    if (tk_consume(',')) continue;
    tk_expect(')');
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
