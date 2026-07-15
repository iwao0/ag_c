#ifndef PARSER_FUNCTION_PUBLIC_H
#define PARSER_FUNCTION_PUBLIC_H

#include "core.h"
#include "type.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct psx_function_symbol_t psx_function_symbol_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

bool ps_ctx_has_function_name_in(
    psx_semantic_context_t *context, char *name, int len);
int ps_ctx_is_function_defined_in(
    psx_semantic_context_t *context, char *name, int len);
const psx_function_symbol_t *ps_ctx_find_function_symbol_in(
    psx_semantic_context_t *context, char *name, int len);
const psx_type_t *ps_function_symbol_type(
    const psx_function_symbol_t *symbol);
const psx_type_t *ps_ctx_get_function_type_in(
    psx_semantic_context_t *context, char *name, int len);
/* Returns the canonical C signature length, or -1 when the function is unknown.
 * A zero-sized output queries the required length; for example void(void) is v(). */
int ps_ctx_format_function_signature_in(
    psx_semantic_context_t *context, char *name, int len,
    char *out, size_t out_size);
int ps_ctx_scalar_type_size(token_kind_t kind);

#endif
