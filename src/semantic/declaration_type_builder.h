#ifndef SEMANTIC_DECLARATION_TYPE_BUILDER_H
#define SEMANTIC_DECLARATION_TYPE_BUILDER_H

#include "declaration_resolution.h"

/* Mutable specifier construction stays inside declaration semantic owners. */
psx_type_t *psx_build_decl_specifier_type_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier);

#endif
