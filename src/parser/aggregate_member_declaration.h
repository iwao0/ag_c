#ifndef PARSER_AGGREGATE_MEMBER_DECLARATION_H
#define PARSER_AGGREGATE_MEMBER_DECLARATION_H

#include "../semantic/aggregate_member_resolution.h"
#include "core.h"

int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok);

#endif
