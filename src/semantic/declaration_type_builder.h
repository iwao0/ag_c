#ifndef SEMANTIC_DECLARATION_TYPE_BUILDER_H
#define SEMANTIC_DECLARATION_TYPE_BUILDER_H

#include "declaration_resolution.h"

/* Mutable construction results stay inside semantic resolution. Publish them
 * through the const-returning APIs in declaration_resolution.h. */
psx_type_t *psx_build_decl_type(const psx_decl_type_request_t *request);
psx_type_t *psx_build_decl_specifier_type(
    const psx_parsed_decl_specifier_t *specifier);

#endif
