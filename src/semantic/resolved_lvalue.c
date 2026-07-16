#include "resolved_lvalue.h"

#include "resolved_node_kind.h"
#include "resolved_node_type.h"
#include "resolved_object_ref.h"
#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"

static int is_lvalue_clone_kind(psx_work_node_kind_t kind) {
  return kind == ND_LVAR || kind == ND_GVAR || kind == ND_UNARY_DEREF ||
         kind == ND_SUBSCRIPT || kind == ND_DEREF || kind == ND_STRING;
}

void ps_node_bind_symbol_decl_type_if_missing(node_t *node) {
  if (!node || ps_node_get_type(node)) return;
  switch (psx_resolved_object_ref_kind(node)) {
    case PSX_RESOLVED_OBJECT_REF_LOCAL: {
      lvar_t *var = psx_resolved_object_ref_local(node);
      const psx_type_t *type = ps_lvar_get_decl_type(var);
      if (type)
        ps_node_bind_qual_type(
            node, type, ps_lvar_decl_qual_type(var));
      return;
    }
    case PSX_RESOLVED_OBJECT_REF_GLOBAL: {
      global_var_t *var = psx_resolved_object_ref_global(node);
      const psx_type_t *type = ps_gvar_get_decl_type(var);
      if (type)
        ps_node_bind_qual_type(
            node, type, ps_gvar_decl_qual_type(var));
      return;
    }
    case PSX_RESOLVED_OBJECT_REF_NONE:
    case PSX_RESOLVED_OBJECT_REF_FUNCTION:
    case PSX_RESOLVED_OBJECT_REF_VA_ARG_AREA:
      return;
  }
}

node_t *ps_node_clone_lvalue_with_lhs_in(
    arena_context_t *arena_context, node_t *target, node_t *lhs) {
  if (!target || !is_lvalue_clone_kind(target->kind)) return target;
  switch (target->kind) {
    case ND_LVAR: {
      node_t *clone = psx_resolution_node_alloc_in(
          arena_context, sizeof(*clone));
      *clone = *target;
      ps_node_copy_resolution_state_in(
          arena_context, clone, target);
      clone->lhs = lhs;
      return clone;
    }
    case ND_GVAR: {
      node_t *clone = psx_resolution_node_alloc_in(
          arena_context, sizeof(*clone));
      *clone = *target;
      ps_node_copy_resolution_state_in(
          arena_context, clone, target);
      clone->lhs = lhs;
      return clone;
    }
    case ND_STRING: {
      node_string_t *clone = psx_resolution_node_alloc_in(
          arena_context, sizeof(*clone));
      *clone = *(node_string_t *)target;
      ps_node_copy_resolution_state_in(
          arena_context, (node_t *)clone, target);
      clone->base.lhs = lhs;
      return (node_t *)clone;
    }
    case ND_UNARY_DEREF:
    case ND_DEREF: {
      node_t *clone = psx_resolution_node_alloc_in(
          arena_context, sizeof(*clone));
      *clone = *target;
      ps_node_copy_resolution_state_in(
          arena_context, clone, target);
      clone->lhs = lhs;
      return clone;
    }
    default:
      return target;
  }
}
