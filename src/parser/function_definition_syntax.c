#include "function_definition_syntax.h"

#include <string.h>
#include <stdlib.h>

void ps_move_toplevel_declaration_head_to_function_definition(
    psx_parsed_toplevel_declaration_t *declaration,
    psx_parsed_function_definition_t *definition) {
  if (!declaration || !definition || declaration->declarator_count != 1)
    return;
  *definition = (psx_parsed_function_definition_t){0};
  definition->return_specifier = declaration->specifier;
  declaration->specifier = (psx_parsed_decl_specifier_t){0};
  definition->declarator = declaration->declarators[0];
  declaration->declarators[0] = (psx_parsed_declarator_t){0};
  definition->is_static = declaration->is_static;
  definition->has_implicit_int_return =
      definition->return_specifier.source ==
      PSX_PARSED_DECL_TYPE_IMPLICIT_INT;
  definition->diagnostic_token = declaration->diagnostic_token;
  free(declaration->declarators);
  free(declaration->initializers);
  *declaration = (psx_parsed_toplevel_declaration_t){0};
}

void ps_dispose_function_definition_header_syntax(
    psx_parsed_function_definition_t *definition) {
  if (!definition) return;
  ps_dispose_declarator_syntax(&definition->declarator);
  ps_dispose_decl_specifier_syntax(&definition->return_specifier);
  memset(definition, 0, sizeof(*definition));
}
