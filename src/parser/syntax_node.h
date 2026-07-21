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
node_t *psx_node_new_null_statement_syntax_in(
    arena_context_t *arena_context, token_t *token);
node_t *psx_node_new_static_assert_syntax_in(
    arena_context_t *arena_context, node_t *condition, token_t *token);
node_t *psx_node_new_compound_literal_in(
    arena_context_t *arena_context,
    psx_type_name_ref_t type_name, node_t *initializer, token_t *token);
node_t *psx_node_new_raw_decl_initializer_in(
    arena_context_t *arena_context, node_t *target, node_t *value,
    psx_decl_init_kind_t init_kind, token_t *token);
node_t *psx_node_new_raw_decl_initializer_list_in(
    arena_context_t *arena_context,
    node_t *target, psx_decl_init_kind_t init_kind,
    psx_initializer_entry_t *entries, int entry_count, token_t *token);
node_t *psx_node_new_initializer_list_in(
    arena_context_t *arena_context,
    psx_initializer_entry_t *entries, int entry_count, token_t *token);

#endif
