#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

#include "core.h"
#include "arena.h"
#include "ast.h"
#include "init_slot.h"
#include "gvar_public.h"
#include "../semantic/resolved_node_type.h"
#include "../semantic/resolved_node_kind.h"
#include "node_vla_public.h"
#include "syntax_node.h"
#include "tag_public.h"
#include "../target_info.h"

struct lvar_t;
struct global_var_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

int ps_node_is_long_long_type(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_is_plain_char_type(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_is_long_double_type(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_integer_value_is_unsigned(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_scalar_ptr_member_lvalue(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_subscript_deref_uses_base_address(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_bitfield_width(
    const psx_resolution_store_t *store, node_t *node);

node_t *ps_node_new_comma_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *lhs, node_t *rhs);
struct psx_vla_runtime_plan_t;
node_t *ps_node_new_vla_runtime_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    struct psx_vla_runtime_plan_t *runtime_plan);
node_t *psx_node_new_raw_decl_initializer_in(
    arena_context_t *arena_context, node_t *target, node_t *value,
    psx_decl_init_kind_t init_kind, token_t *tok);
node_t *psx_node_new_raw_decl_initializer_list_in(
    arena_context_t *arena_context,
    node_t *target, psx_decl_init_kind_t init_kind,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok);
node_t *psx_node_new_initializer_list_in(
    arena_context_t *arena_context,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok);

void ps_node_reject_const_assign_at_in(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const char *op, token_t *tok);
void ps_node_reject_const_qual_discard_at_in(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    token_t *tok);
int ps_node_is_lvalue_in(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_expect_lvalue_at_in(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const char *op, token_t *tok);

#endif
