#ifndef PARSER_SYNTAX_NODE_H
#define PARSER_SYNTAX_NODE_H

#include "arena.h"
#include "ast.h"

node_t *psx_node_new_raw_binary_in(
    arena_context_t *arena_context, psx_syntax_node_kind_t kind,
    node_t *lhs, node_t *rhs);
node_t *psx_node_new_syntax_int_in(
    arena_context_t *arena_context, long long value, token_t *token);
node_t *psx_node_new_source_cast_in(
    arena_context_t *arena_context, node_t *operand,
    psx_type_name_ref_t type_name);
node_t *psx_node_new_unary_deref_syntax_for_in(
    arena_context_t *arena_context, node_t *operand);
node_t *psx_node_new_unary_addr_syntax_for_in(
    arena_context_t *arena_context, node_t *operand);
node_t *psx_node_new_subscript_syntax_for_in(
    arena_context_t *arena_context, node_t *base, node_t *index);
node_t *psx_node_new_raw_assign_in(
    arena_context_t *arena_context, node_t *lhs, node_t *rhs);
node_t *psx_node_new_static_assert_syntax_in(
    arena_context_t *arena_context, node_t *condition, token_t *token);
node_t *psx_node_new_compound_literal_in(
    arena_context_t *arena_context,
    psx_type_name_ref_t type_name, node_t *initializer, token_t *token,
    int requires_addressable_object, int has_file_scope_storage);

#endif
