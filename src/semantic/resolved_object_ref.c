#include "resolved_object_ref.h"

#include "type_identity.h"

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_internal.h"
#include "../parser/node_vla_public.h"
#include "../parser/symtab.h"
#include "../parser/type_builder.h"
#include "resolved_node_kind.h"
#include "resolved_node_type.h"
#include "resolution_state.h"

static psx_resolved_reference_state_t *reference_state(
    psx_resolution_store_t *store, node_t *node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  return state ? &state->reference : NULL;
}

static int bind_local_reference_payload_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node, lvar_t *var,
    int storage_offset) {
  if (!node ||
      !ps_node_prepare_resolution_state_in(store, arena_context, node))
    return 0;
  psx_resolved_reference_state_t *reference = reference_state(store, node);
  if (!reference) return 0;
  *reference = (psx_resolved_reference_state_t){
      .local = var,
      .storage_offset = storage_offset,
      .kind = PSX_RESOLVED_REFERENCE_LOCAL,
  };
  return 1;
}

static void bind_local_reference_vla_runtime(
    psx_resolution_store_t *store, node_t *node,
    const lvar_t *var) {
  if (!var) return;
  ps_node_set_vla_runtime_view(
      store, node, ps_lvar_vla_row_stride_frame_off(var),
      ps_lvar_vla_strides_remaining(var));
}

int psx_bind_local_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node, lvar_t *var,
    int storage_offset, const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t qual_type) {
  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      semantic_types, qual_type);
  if (!type ||
      !bind_local_reference_payload_in(
          store, arena_context, node, var, storage_offset))
    return 0;
  ps_node_bind_qual_type(store, node, type, qual_type);
  bind_local_reference_vla_runtime(store, node, var);
  return 1;
}

static int bind_local_reference_type_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node, lvar_t *var,
    int storage_offset, const psx_type_t *type) {
  if (!type ||
      !bind_local_reference_payload_in(
          store, arena_context, node, var, storage_offset))
    return 0;
  ps_node_bind_type(store, node, type);
  bind_local_reference_vla_runtime(store, node, var);
  return 1;
}

int psx_bind_global_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node,
    global_var_t *global, char *name, int name_len,
    const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t qual_type,
    int is_thread_local) {
  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      semantic_types, qual_type);
  if (!node || !type ||
      !ps_node_prepare_resolution_state_in(store, arena_context, node))
    return 0;
  ps_node_bind_qual_type(store, node, type, qual_type);
  psx_resolved_reference_state_t *reference = reference_state(store, node);
  if (!reference) return 0;
  *reference = (psx_resolved_reference_state_t){
      .global = global,
      .name = name,
      .name_len = name_len,
      .kind = PSX_RESOLVED_REFERENCE_GLOBAL,
      .is_thread_local = is_thread_local ? 1 : 0,
  };
  return 1;
}

int psx_bind_function_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node,
    char *name, int name_len,
    const psx_semantic_type_table_t *types,
    psx_qual_type_t function_qual_type) {
  if (!node ||
      !ps_node_prepare_resolution_state_in(store, arena_context, node))
    return 0;
  const psx_type_t *function_type =
      psx_semantic_type_table_lookup_qual_type(
          types, function_qual_type);
  if (!function_type) return 0;
  ps_node_bind_qual_type(store, node, function_type, function_qual_type);
  psx_resolved_reference_state_t *reference = reference_state(store, node);
  if (!reference) return 0;
  *reference = (psx_resolved_reference_state_t){
      .name = name,
      .name_len = name_len,
      .kind = PSX_RESOLVED_REFERENCE_FUNCTION,
  };
  return 1;
}

int psx_bind_va_arg_area_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node) {
  if (!node ||
      !ps_node_prepare_resolution_state_in(store, arena_context, node))
    return 0;
  psx_resolved_reference_state_t *reference = reference_state(store, node);
  if (!reference) return 0;
  *reference = (psx_resolved_reference_state_t){
      .kind = PSX_RESOLVED_REFERENCE_VARARG_CURSOR,
  };
  return 1;
}

psx_resolution_node_kind_t psx_resolved_object_ref_node_kind(
    const psx_resolution_store_t *store, const node_t *node) {
  if (!node || node->kind != ND_IDENTIFIER)
    return psx_resolution_node_kind(store, node);
  switch (psx_resolved_object_ref_kind(store, node)) {
    case PSX_RESOLVED_OBJECT_REF_LOCAL: return ND_LVAR;
    case PSX_RESOLVED_OBJECT_REF_GLOBAL: return ND_GVAR;
    case PSX_RESOLVED_OBJECT_REF_FUNCTION: return ND_FUNCREF;
    case PSX_RESOLVED_OBJECT_REF_VARARG_CURSOR: return ND_VARARG_CURSOR;
    case PSX_RESOLVED_OBJECT_REF_NONE: return ND_IDENTIFIER;
  }
  return ND_IDENTIFIER;
}

static node_t *new_lvar_symbol_node(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, lvar_t *var,
    const psx_type_t *type) {
  node_t *node = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node ||
      !psx_resolution_node_set_kind(store, node, ND_LVAR))
    return NULL;
  return bind_local_reference_type_in(
             store, arena_context, node, var, offset, type)
             ? node : NULL;
}

static node_t *new_decl_lvar_symbol_node(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    lvar_t *var, int offset) {
  if (!semantic_types || !var) return NULL;
  node_t *node = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node ||
      !psx_resolution_node_set_kind(store, node, ND_LVAR) ||
      !psx_bind_local_reference_in(
          store, arena_context, node, var, offset, semantic_types,
          ps_lvar_decl_qual_type(var)))
    return NULL;
  return node;
}

static psx_integer_kind_t integer_kind_for_storage_size(int size) {
  if (size <= 1) return PSX_INTEGER_KIND_CHAR;
  if (size == 2) return PSX_INTEGER_KIND_SHORT;
  if (size >= 8) return PSX_INTEGER_KIND_LONG;
  return PSX_INTEGER_KIND_INT;
}

node_t *psx_node_new_lvar_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset) {
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, NULL,
      ps_type_new_integer_kind_in(
          arena_context, PSX_INTEGER_KIND_LONG, 0, 0));
}

node_t *ps_node_new_lvar_typed_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size) {
  int size = type_size > 0 ? type_size : 8;
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, NULL,
      ps_type_new_integer_kind_in(
          arena_context, integer_kind_for_storage_size(size), 0, 0));
}

node_t *ps_node_new_lvar_storage_slot_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *owner, int offset,
    int type_size) {
  const psx_type_t *type = ps_type_new_integer_kind_in(
      arena_context,
      integer_kind_for_storage_size(type_size > 0 ? type_size : 8),
      0, 0);
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, owner, type);
}

node_t *ps_node_new_lvar_type_at_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *owner, int offset,
    const psx_type_t *type) {
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, owner, type);
}

node_t *psx_node_new_lvar_scalar_slot_at_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size,
    psx_floating_kind_t floating_kind, int is_bool) {
  psx_type_t *type = floating_kind != PSX_FLOATING_KIND_NONE
                         ? ps_type_new_floating_in(
                               arena_context, floating_kind, 0)
                     : is_bool
                         ? ps_type_new_integer_kind_in(
                               arena_context, PSX_INTEGER_KIND_BOOL,
                               1, 0)
                         : ps_type_new_integer_kind_in(
                               arena_context,
                               integer_kind_for_storage_size(type_size),
                               0, 0);
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, NULL, type);
}

node_t *psx_node_new_lvar_fp_slot_at_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size,
    psx_floating_kind_t floating_kind) {
  return psx_node_new_lvar_scalar_slot_at_in(
      store, arena_context, offset, type_size, floating_kind, 0);
}

node_t *ps_node_new_lvar_fp_slot_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    lvar_t *owner, int offset, int type_size) {
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      semantic_types, ps_lvar_decl_type_id(owner));
  psx_type_shape_t leaf_shape = {0};
  int has_leaf = psx_semantic_type_table_describe(
      semantic_types, leaf.type_id, &leaf_shape);
  psx_floating_kind_t floating_kind =
      has_leaf && (leaf_shape.kind == PSX_TYPE_FLOAT ||
                   leaf_shape.kind == PSX_TYPE_COMPLEX)
          ? leaf_shape.floating_kind
          : PSX_FLOATING_KIND_NONE;
  psx_type_t *type = floating_kind != PSX_FLOATING_KIND_NONE
                         ? ps_type_new_floating_in(
                               arena_context, floating_kind, 0)
                         : ps_type_new_integer_kind_in(
                               arena_context,
                               integer_kind_for_storage_size(type_size),
                               0, 0);
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, owner, type);
}

node_t *ps_node_new_param_placeholder_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const psx_type_t *type) {
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, 0, NULL, type);
}

node_t *ps_node_new_unsigned_lvar_typed_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size) {
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, offset, NULL,
      ps_type_new_integer_kind_in(
          arena_context, integer_kind_for_storage_size(type_size),
          1, 0));
}

node_t *psx_node_new_lvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var) {
  return new_decl_lvar_symbol_node(
      store, arena_context, semantic_types, var, var ? var->offset : 0);
}

node_t *psx_node_new_lvar_object_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var) {
  return psx_node_new_lvar_for_in(
      store, arena_context, semantic_types, var);
}

node_t *ps_node_new_lvar_expr_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var) {
  return new_decl_lvar_symbol_node(
      store, arena_context, semantic_types, var, var ? var->offset : 0);
}

node_t *ps_node_new_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global);
node_t *psx_node_new_static_local_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);

node_t *psx_node_new_lvar_identifier_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var) {
  if (var && var->is_static_local && var->static_global_name)
    return psx_node_new_static_local_gvar_for_in(
        store, arena_context, semantic_types, var);
  return psx_node_new_lvar_for_in(
      store, arena_context, semantic_types, var);
}

node_t *psx_node_new_vla_decay_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var,
    psx_qual_type_t decay_qual_type) {
  if (!semantic_types || !var ||
      decay_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return psx_node_new_lvar_identifier_ref_for_in(
        store, arena_context, semantic_types, var);
  node_t *node = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node ||
      !psx_resolution_node_set_kind(store, node, ND_LVAR) ||
      !psx_bind_local_reference_in(
          store, arena_context, node, var, var->offset,
          semantic_types, decay_qual_type))
    return NULL;
  return node;
}

node_t *ps_node_new_param_lvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var) {
  return new_decl_lvar_symbol_node(
      store, arena_context, semantic_types, var, var ? var->offset : 0);
}

static node_t *new_address_node(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *base) {
  node_t *address = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*address));
  if (!address ||
      !psx_resolution_node_set_kind(store, address, ND_ADDR))
    return NULL;
  address->lhs = base;
  return address;
}

static int bind_array_address_qual_type(
    psx_resolution_store_t *store,
    const psx_semantic_type_table_t *semantic_types,
    node_t *address, psx_qual_type_t expression_qual_type) {
  psx_type_shape_t shape = {0};
  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      semantic_types, expression_qual_type);
  if (!address || !type ||
      !psx_semantic_type_table_describe(
          semantic_types, expression_qual_type.type_id, &shape) ||
      (shape.kind != PSX_TYPE_POINTER && shape.kind != PSX_TYPE_ARRAY))
    return 0;
  ps_node_bind_qual_type(
      store, address, type, expression_qual_type);
  return 1;
}

node_t *ps_node_new_gvar_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global, psx_qual_type_t expression_qual_type) {
  node_t *address = new_address_node(
      store, arena_context,
      psx_node_new_gvar_array_base_for_in(
          store, arena_context, semantic_types, global));
  return bind_array_address_qual_type(
             store, semantic_types, address, expression_qual_type)
             ? address : NULL;
}

node_t *psx_node_new_static_local_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    lvar_t *var, psx_qual_type_t expression_qual_type) {
  node_t *address = new_address_node(
      store, arena_context,
      psx_node_new_static_local_gvar_for_in(
          store, arena_context, semantic_types, var));
  return bind_array_address_qual_type(
             store, semantic_types, address, expression_qual_type)
             ? address : NULL;
}

node_t *ps_node_new_lvar_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var,
    psx_qual_type_t expression_qual_type) {
  node_t *address = new_address_node(
      store, arena_context,
      psx_node_new_lvar_for_in(
          store, arena_context, semantic_types, var));
  return bind_array_address_qual_type(
             store, semantic_types, address, expression_qual_type)
             ? address : NULL;
}

static psx_type_t *type_with_object_qualifiers_in(
    arena_context_t *arena_context, const psx_type_t *type,
    int is_const, int is_volatile) {
  if (!type) return NULL;
  psx_type_t *copy = arena_alloc_in(arena_context, sizeof(*copy));
  if (!copy) return NULL;
  *copy = *type;
  if (copy->kind == PSX_TYPE_ARRAY && copy->base) {
    copy->base = type_with_object_qualifiers_in(
        arena_context, copy->base, is_const, is_volatile);
    return copy;
  }
  psx_type_qualifiers_t qualifiers = PSX_TYPE_QUALIFIER_NONE;
  if (is_const) qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  if (is_volatile) qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
  ps_type_add_qualifiers(copy, qualifiers);
  return copy;
}

node_t *ps_node_new_tag_member_lvar_ref_with_layout_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *owner,
    int member_offset, psx_qual_type_t member_qual_type,
    int bit_is_signed, int bit_width, int bit_offset) {
  const psx_type_t *member_type =
      psx_semantic_type_table_lookup_qual_type(
          semantic_types, member_qual_type);
  if (member_type) {
    psx_qual_type_t owner_value = ps_lvar_decl_qual_type(owner);
    psx_type_shape_t owner_shape = {0};
    if (psx_semantic_type_table_describe(
            semantic_types, owner_value.type_id, &owner_shape) &&
        owner_shape.kind == PSX_TYPE_ARRAY) {
      owner_value = psx_semantic_type_table_array_leaf(
          semantic_types, owner_value.type_id);
    }
    member_type = type_with_object_qualifiers_in(
        arena_context, member_type,
        (owner_value.qualifiers & PSX_TYPE_QUALIFIER_CONST) != 0,
        (owner_value.qualifiers & PSX_TYPE_QUALIFIER_VOLATILE) != 0);
  }
  if (!member_type) {
    member_type = ps_type_new_integer_kind_in(
        arena_context, PSX_INTEGER_KIND_INT, 0, 0);
  }
  node_t *node = new_lvar_symbol_node(
      store, arena_context, (owner ? owner->offset : 0) + member_offset,
      owner, member_type);
  if (!node) return NULL;
  ps_node_set_bitfield_info(
      store, node, bit_width, bit_offset, bit_is_signed);
  return node;
}

static node_t *new_global_symbol_node(
    psx_resolution_store_t *store,
    arena_context_t *arena_context) {
  node_t *node = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (node && !psx_resolution_node_set_kind(store, node, ND_GVAR))
    return NULL;
  return node;
}

node_t *ps_node_new_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global) {
  node_t *node = new_global_symbol_node(store, arena_context);
  if (!node) return NULL;
  if (!global) return node;
  return psx_bind_global_reference_in(
             store, arena_context, node, global,
             global->name, global->name_len, semantic_types,
             ps_gvar_decl_qual_type(global), global->is_thread_local)
             ? node : NULL;
}

node_t *psx_node_new_gvar_array_base_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global) {
  return ps_node_new_gvar_for_in(
      store, arena_context, semantic_types, global);
}

node_t *psx_node_new_static_local_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var) {
  node_t *node = new_global_symbol_node(store, arena_context);
  if (!node) return NULL;
  if (!var) return node;
  psx_qual_type_t declaration_qual_type = var->static_global
      ? ps_gvar_decl_qual_type(var->static_global)
      : ps_lvar_decl_qual_type(var);
  return psx_bind_global_reference_in(
             store, arena_context, node, var->static_global,
             var->static_global_name, var->static_global_name_len,
             semantic_types, declaration_qual_type,
             var->static_global && var->static_global->is_thread_local)
             ? node : NULL;
}

node_t *psx_node_new_function_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, char *name, int name_len,
    const psx_type_t *function_type) {
  node_t *reference = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*reference));
  if (!reference ||
      !psx_resolution_node_set_kind(store, reference, ND_FUNCREF))
    return NULL;
  ps_node_bind_type(
      store, reference,
      function_type
          ? ps_type_clone_in(arena_context, function_type)
          : NULL);
  psx_resolved_reference_state_t *state =
      reference_state(store, reference);
  if (state) {
    state->kind = PSX_RESOLVED_REFERENCE_FUNCTION;
    state->name = name;
    state->name_len = name_len;
  }
  return reference;
}

node_t *psx_node_new_va_arg_area_reference_in(
    psx_resolution_store_t *store, arena_context_t *arena_context) {
  node_t *node = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (node) {
    if (!psx_resolution_node_set_kind(store, node, ND_VARARG_CURSOR))
      return NULL;
    psx_resolved_reference_state_t *state =
        reference_state(store, node);
    if (state)
      state->kind = PSX_RESOLVED_REFERENCE_VARARG_CURSOR;
  }
  return node;
}

lvar_t *ps_node_lvar_symbol(
    const psx_resolution_store_t *store, node_t *node) {
  return psx_resolved_object_ref_local(store, node);
}

psx_resolved_object_ref_kind_t psx_resolved_object_ref_kind(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  if (!state) return PSX_RESOLVED_OBJECT_REF_NONE;
  switch (state->reference.kind) {
    case PSX_RESOLVED_REFERENCE_LOCAL:
      return PSX_RESOLVED_OBJECT_REF_LOCAL;
    case PSX_RESOLVED_REFERENCE_GLOBAL:
      return PSX_RESOLVED_OBJECT_REF_GLOBAL;
    case PSX_RESOLVED_REFERENCE_FUNCTION:
      return PSX_RESOLVED_OBJECT_REF_FUNCTION;
    case PSX_RESOLVED_REFERENCE_VARARG_CURSOR:
      return PSX_RESOLVED_OBJECT_REF_VARARG_CURSOR;
    case PSX_RESOLVED_REFERENCE_NONE:
      return PSX_RESOLVED_OBJECT_REF_NONE;
  }
  return PSX_RESOLVED_OBJECT_REF_NONE;
}

lvar_t *psx_resolved_object_ref_local(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state &&
                 state->reference.kind == PSX_RESOLVED_REFERENCE_LOCAL
             ? state->reference.local
             : NULL;
}

global_var_t *psx_resolved_object_ref_global(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state &&
                 state->reference.kind == PSX_RESOLVED_REFERENCE_GLOBAL
             ? state->reference.global
             : NULL;
}

int psx_resolved_object_ref_storage_offset(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state &&
                 state->reference.kind == PSX_RESOLVED_REFERENCE_LOCAL
             ? state->reference.storage_offset
             : 0;
}

char *psx_resolved_object_ref_name(
    const psx_resolution_store_t *store,
    const node_t *node, int *name_len) {
  if (name_len) *name_len = 0;
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  if (!state ||
      (state->reference.kind != PSX_RESOLVED_REFERENCE_GLOBAL &&
       state->reference.kind != PSX_RESOLVED_REFERENCE_FUNCTION))
    return NULL;
  if (name_len) *name_len = state->reference.name_len;
  return state->reference.name;
}

int psx_resolved_object_ref_is_thread_local(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state &&
         state->reference.kind == PSX_RESOLVED_REFERENCE_GLOBAL &&
         state->reference.is_thread_local;
}
