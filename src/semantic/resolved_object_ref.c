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

int psx_bind_local_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node, lvar_t *var,
    int storage_offset, const psx_type_t *type) {
  if (!node ||
      !ps_node_prepare_resolution_state_in(store, arena_context, node))
    return 0;
  if (var && type == ps_lvar_get_decl_type(var)) {
    ps_node_bind_qual_type(
        store, node, type, ps_lvar_decl_qual_type(var));
  } else {
    ps_node_bind_type(store, node, type);
  }
  psx_resolved_reference_state_t *reference = reference_state(store, node);
  if (!reference) return 0;
  *reference = (psx_resolved_reference_state_t){
      .local = var,
      .storage_offset = storage_offset,
      .kind = PSX_RESOLVED_REFERENCE_LOCAL,
  };
  if (var) {
    ps_node_set_vla_runtime_view(
        store, node, ps_lvar_vla_row_stride_frame_off(var),
        ps_lvar_vla_strides_remaining(var));
  }
  return 1;
}

int psx_bind_global_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node,
    global_var_t *global, char *name, int name_len,
    const psx_type_t *type, psx_qual_type_t qual_type,
    int is_thread_local) {
  if (!node ||
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
  return psx_bind_local_reference_in(
             store, arena_context, node, var, offset, type)
             ? node : NULL;
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
    arena_context_t *arena_context, lvar_t *owner, int offset,
    int type_size) {
  const psx_type_t *owner_type = ps_lvar_get_decl_type(owner);
  const psx_type_t *leaf = ps_type_array_leaf_type(owner_type);
  psx_floating_kind_t floating_kind =
      leaf && (leaf->kind == PSX_TYPE_FLOAT ||
               leaf->kind == PSX_TYPE_COMPLEX)
          ? leaf->floating_kind
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
    arena_context_t *arena_context, lvar_t *var) {
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, var ? var->offset : 0, var, type);
}

node_t *psx_node_new_lvar_object_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  return psx_node_new_lvar_for_in(store, arena_context, var);
}

node_t *ps_node_new_lvar_expr_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, var ? var->offset : 0, var, type);
}

node_t *ps_node_new_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, global_var_t *global);
node_t *psx_node_new_static_local_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var);

node_t *psx_node_new_lvar_identifier_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  if (var && var->is_static_local && var->static_global_name)
    return psx_node_new_static_local_gvar_for_in(
        store, arena_context, var);
  return psx_node_new_lvar_for_in(store, arena_context, var);
}

node_t *psx_node_new_vla_decay_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  const psx_type_t *array_type = var ? ps_lvar_get_decl_type(var) : NULL;
  const psx_type_t *decay_type = ps_type_decay_array_in(
      arena_context, array_type);
  if (!decay_type)
    return psx_node_new_lvar_identifier_ref_for_in(
        store, arena_context, var);
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, var->offset, var, decay_type);
}

node_t *ps_node_new_param_lvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  const psx_type_t *type = var ? ps_lvar_get_decl_type(var) : NULL;
  return (node_t *)new_lvar_symbol_node(
      store, arena_context, var ? var->offset : 0, var, type);
}

static node_t *new_address_node(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *base) {
  node_t *address = psx_resolution_node_alloc_in(
      store, arena_context, sizeof(*address));
  if (!address) return NULL;
  address->kind = ND_ADDR;
  address->lhs = base;
  return address;
}

static int is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY);
}

static void bind_array_address_type(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *address,
    const psx_type_t *array_type) {
  if (!address || !array_type) return;
  ps_node_bind_type(
      store, address,
      array_type->kind == PSX_TYPE_ARRAY
          ? ps_type_decay_array_in(arena_context, array_type)
          : (is_pointer_view_type(array_type) ? array_type : NULL));
}

node_t *ps_node_new_gvar_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, global_var_t *global) {
  node_t *address = new_address_node(
      store, arena_context,
      psx_node_new_gvar_array_base_for_in(store, arena_context, global));
  bind_array_address_type(
      store, arena_context, address, ps_gvar_get_decl_type(global));
  return address;
}

node_t *psx_node_new_static_local_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  node_t *address = new_address_node(
      store, arena_context,
      psx_node_new_static_local_gvar_for_in(store, arena_context, var));
  const psx_type_t *backing_type =
      var && var->static_global
          ? ps_gvar_get_decl_type(var->static_global)
          : NULL;
  bind_array_address_type(store, arena_context, address, backing_type);
  return address;
}

node_t *ps_node_new_lvar_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  node_t *address = new_address_node(
      store, arena_context,
      psx_node_new_lvar_for_in(store, arena_context, var));
  bind_array_address_type(
      store, arena_context, address, ps_lvar_get_decl_type(var));
  return address;
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
    arena_context_t *arena_context, lvar_t *owner,
    int member_offset, const psx_type_t *member_type,
    int bit_is_signed, int bit_width, int bit_offset) {
  if (member_type) {
    const psx_type_t *owner_type = ps_lvar_get_decl_type(owner);
    const psx_type_t *owner_value =
        ps_type_array_leaf_type(owner_type);
    member_type = type_with_object_qualifiers_in(
        arena_context, member_type,
        ps_type_has_qualifier(
            owner_value, PSX_TYPE_QUALIFIER_CONST),
        ps_type_has_qualifier(
            owner_value, PSX_TYPE_QUALIFIER_VOLATILE));
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
    arena_context_t *arena_context, global_var_t *global) {
  node_t *node = new_global_symbol_node(store, arena_context);
  if (!node) return NULL;
  if (global) {
    ps_node_bind_qual_type(
        store, node, ps_gvar_get_decl_type(global),
        ps_gvar_decl_qual_type(global));
  }
  psx_resolved_reference_state_t *reference =
      reference_state(store, node);
  if (reference) {
    reference->kind = PSX_RESOLVED_REFERENCE_GLOBAL;
    reference->global = global;
    reference->name = global ? global->name : NULL;
    reference->name_len = global ? global->name_len : 0;
    reference->is_thread_local =
        global && global->is_thread_local ? 1 : 0;
  }
  return node;
}

node_t *psx_node_new_gvar_array_base_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, global_var_t *global) {
  return ps_node_new_gvar_for_in(store, arena_context, global);
}

node_t *psx_node_new_static_local_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var) {
  node_t *node = new_global_symbol_node(store, arena_context);
  if (!node) return NULL;
  if (var) {
    const psx_type_t *type = var->static_global
                                 ? ps_gvar_get_decl_type(
                                       var->static_global)
                                 : NULL;
    if (type && var->static_global) {
      ps_node_bind_qual_type(
          store, node, type,
          ps_gvar_decl_qual_type(var->static_global));
    } else {
      type = ps_lvar_get_decl_type(var);
      ps_node_bind_qual_type(
          store, node, type, ps_lvar_decl_qual_type(var));
    }
  }
  psx_resolved_reference_state_t *reference =
      reference_state(store, node);
  if (reference) {
    reference->kind = PSX_RESOLVED_REFERENCE_GLOBAL;
    reference->global = var ? var->static_global : NULL;
    reference->name = var ? var->static_global_name : NULL;
    reference->name_len = var ? var->static_global_name_len : 0;
  }
  return node;
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
