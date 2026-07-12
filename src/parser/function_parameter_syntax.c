#include "function_parameter_syntax.h"

#include "diag.h"
#include "dynarray.h"
#include "semantic_ctx.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static int is_parameter_typedef_name(token_t *token, void *context) {
  (void)context;
  return psx_ctx_is_typedef_name_token(token);
}

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

void psx_parse_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters) {
  psx_parse_function_parameters_syntax_ex(parameters, 0);
}

void psx_parse_function_parameters_syntax_ex(
    psx_parsed_function_parameters_t *parameters,
    int allow_implicit_int) {
  tk_expect('(');
  if (tk_consume(')')) return;
  for (;;) {
    if (current_token()->kind == TK_ELLIPSIS) {
      tk_set_current_token(current_token()->next);
      parameters->is_variadic = 1;
      tk_expect(')');
      return;
    }
    psx_parsed_function_parameter_t *parameter =
        append_function_parameter(parameters);
    if (allow_implicit_int) {
      ps_parse_decl_specifier_syntax_ex(
          &parameter->specifier,
          &(psx_decl_specifier_syntax_options_t){
              .is_typedef_name = is_parameter_typedef_name,
              .allow_implicit_int = 1,
          });
    } else {
      psx_parse_decl_specifier_syntax(&parameter->specifier);
    }
    parameter->declarator =
        psx_parse_parameter_declarator_syntax_tree(
            is_parameter_typedef_name, NULL);
    if (tk_consume(',')) continue;
    tk_expect(')');
    return;
  }
}

void psx_dispose_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters) {
  if (!parameters) return;
  for (int i = 0; i < parameters->count; i++) {
    ps_dispose_decl_specifier_syntax(&parameters->items[i].specifier);
    ps_dispose_declarator_syntax(&parameters->items[i].declarator);
  }
  free(parameters->items);
  memset(parameters, 0, sizeof(*parameters));
}
