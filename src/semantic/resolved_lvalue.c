#include "resolved_lvalue.h"

#include "resolved_node_type.h"
#include "resolved_object_ref.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"

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
