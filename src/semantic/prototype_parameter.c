#include "prototype_parameter.h"

#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"
#include "scope_graph.h"
#include "type_identity.h"
#include "../diag/diag.h"
#include "../source_manager.h"

struct psx_prototype_parameter_t {
  psx_qual_type_t declaration_qual_type;
};

const psx_prototype_parameter_t *psx_declare_prototype_parameter_in(
    psx_semantic_context_t *semantic_context,
    char *name, int name_len, psx_qual_type_t declaration_qual_type,
    token_t *diagnostic_token) {
  if (!semantic_context || !name || name_len <= 0)
    return NULL;
  psx_type_shape_t shape = {0};
  psx_scope_graph_t *scope_graph =
      ps_ctx_scope_graph(semantic_context);
  if (!scope_graph ||
      !psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          declaration_qual_type.type_id, &shape) ||
      psx_scope_graph_scope_kind(
          scope_graph, psx_scope_graph_current_scope(scope_graph)) !=
          PSX_SCOPE_FUNCTION_PROTOTYPE)
    return NULL;
  if (psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, psx_scope_graph_current_scope(scope_graph),
          PSX_NAMESPACE_ORDINARY, name, name_len)) {
    ps_diag_duplicate_with_name_in(
        ps_ctx_diagnostics(semantic_context), diagnostic_token,
        "parameter", name, name_len);
    return NULL;
  }
  psx_prototype_parameter_t *parameter = arena_alloc_in(
      ps_ctx_arena(semantic_context), sizeof(*parameter));
  if (!parameter) return NULL;
  parameter->declaration_qual_type = declaration_qual_type;
  psx_decl_id_t declaration_id = psx_scope_graph_declare(
      scope_graph, PSX_NAMESPACE_ORDINARY,
      PSX_DECL_PARAMETER, name, name_len, parameter);
  if (declaration_id == PSX_DECL_ID_INVALID) return NULL;
  if (diagnostic_token) {
    ag_source_manager_t *sources = diag_context_source_manager(
        ps_ctx_diagnostics(semantic_context));
    (void)psx_scope_graph_note_declaration_source(
        scope_graph, declaration_id,
        ag_source_manager_name(sources, diagnostic_token->file_name_id),
        diagnostic_token->source_input, diagnostic_token->byte_offset,
        diagnostic_token->byte_length);
  }
  return parameter;
}

psx_qual_type_t psx_prototype_parameter_qual_type(
    const psx_prototype_parameter_t *parameter) {
  return parameter
             ? parameter->declaration_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}
