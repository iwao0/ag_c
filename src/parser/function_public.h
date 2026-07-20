#ifndef PARSER_FUNCTION_PUBLIC_H
#define PARSER_FUNCTION_PUBLIC_H

#include "../type_system/type_ids.h"
#include <stddef.h>

typedef struct psx_function_symbol_t psx_function_symbol_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

int ps_ctx_is_function_defined_in(
    psx_semantic_context_t *context, char *name, int len);
const psx_function_symbol_t *ps_ctx_find_function_symbol_in(
    psx_semantic_context_t *context, char *name, int len);
psx_qual_type_t ps_function_symbol_qual_type(
    const psx_function_symbol_t *symbol);
psx_qual_type_t ps_ctx_get_function_qual_type_in(
    psx_semantic_context_t *context, char *name, int len);
psx_qual_type_t psx_ctx_get_function_return_qual_type_in(
    psx_semantic_context_t *context, char *name, int len);
/* Returns the canonical C signature length, or -1 when the function is unknown.
 * A zero-sized output queries the required length; for example void(void) is v(). */
int ps_ctx_format_function_signature_in(
    psx_semantic_context_t *context, char *name, int len,
    char *out, size_t out_size);

#endif
