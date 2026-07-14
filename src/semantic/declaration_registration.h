#ifndef FRONTEND_DECLARATION_REGISTRATION_H
#define FRONTEND_DECLARATION_REGISTRATION_H

#include "../parser/ast.h"
#include "aggregate_member_resolution.h"
#include "tag_declaration_resolution.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_apply_parsed_typedef_declaration(
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok);
void psx_apply_parsed_enum_constant(
    char *name, int name_len, long long value, token_t *diag_tok);
void psx_apply_parsed_tag_declaration(
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok);
int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok);
void psx_apply_static_assert(node_t *condition, token_t *diag_tok);
void psx_apply_static_assert_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *condition, token_t *diag_tok);

#endif
