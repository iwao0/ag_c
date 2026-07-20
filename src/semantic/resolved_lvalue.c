#include "resolved_lvalue.h"

#include "resolved_node_type.h"
#include "resolved_object_ref.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"

void ps_node_bind_symbol_decl_type_if_missing(
    psx_resolution_store_t *store, node_t *node) {
  if (!node ||
      ps_node_qual_type(store, node).type_id != PSX_TYPE_ID_INVALID)
    return;
  switch (psx_resolved_object_ref_kind(store, node)) {
    case PSX_RESOLVED_OBJECT_REF_LOCAL: {
      lvar_t *var = psx_resolved_object_ref_local(store, node);
      ps_node_bind_qual_type(
          store, node, ps_lvar_decl_qual_type(var));
      return;
    }
    case PSX_RESOLVED_OBJECT_REF_GLOBAL: {
      global_var_t *var = psx_resolved_object_ref_global(store, node);
      ps_node_bind_qual_type(
          store, node, ps_gvar_decl_qual_type(var));
      return;
    }
    case PSX_RESOLVED_OBJECT_REF_NONE:
    case PSX_RESOLVED_OBJECT_REF_FUNCTION:
    case PSX_RESOLVED_OBJECT_REF_VARARG_CURSOR:
      return;
  }
}
