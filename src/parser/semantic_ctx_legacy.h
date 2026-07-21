#ifndef PARSER_SEMANTIC_CTX_LEGACY_H
#define PARSER_SEMANTIC_CTX_LEGACY_H

#include "semantic_ctx.h"
#include "type.h"

psx_qual_type_t ps_ctx_intern_qual_type_in(
    psx_semantic_context_t *context, const psx_type_t *type);
psx_qual_type_t ps_ctx_intern_declaration_qual_type_in(
    psx_semantic_context_t *context, const psx_type_t *type);
psx_qual_type_t ps_ctx_find_interned_qual_type_in(
    const psx_semantic_context_t *context, const psx_type_t *type);
const psx_type_t *ps_ctx_type_by_id_in(
    const psx_semantic_context_t *context, psx_type_id_t type_id);
psx_type_t *ps_ctx_clone_tag_type_at_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    psx_scope_lookup_point_t point);
void ps_ctx_bind_record_ids_in(
    psx_semantic_context_t *context, psx_type_t *type);

#endif
