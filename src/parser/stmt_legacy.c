#include "stmt_legacy.h"

#include "decl.h"
#include "name_classifier_legacy.h"
#include "semantic_ctx.h"
#include "stmt.h"

int psx_legacy_statement_syntax_adapter_init(
    psx_legacy_statement_syntax_adapter_t *adapter,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!adapter || !semantic_context || !global_registry ||
      !local_registry || !runtime_context)
    return 0;
  char *function_name = NULL;
  int function_name_len = 0;
  ps_decl_get_current_funcname_in(
      local_registry, &function_name, &function_name_len);
  psx_name_classifier_t semantic_classifier =
      ps_ctx_name_classifier(semantic_context);
  if (!psx_legacy_name_classifier_init(
          &adapter->name_classifier,
          name_classifier ? name_classifier : &semantic_classifier,
          local_registry))
    return 0;
  psx_name_classifier_t classifier =
      psx_legacy_name_classifier_view(&adapter->name_classifier);
  return psx_statement_syntax_adapter_init(
      &adapter->syntax, runtime_context, &classifier,
      local_declarations, function_name, function_name_len);
}

psx_statement_syntax_context_t
psx_legacy_statement_syntax_context(
    psx_legacy_statement_syntax_adapter_t *adapter) {
  return psx_statement_syntax_adapter_context(
      adapter ? &adapter->syntax : NULL);
}

node_t *psx_stmt_stmt_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  psx_legacy_statement_syntax_adapter_t adapter;
  if (!psx_legacy_statement_syntax_adapter_init(
          &adapter, semantic_context, global_registry, local_registry,
          runtime_context, name_classifier, local_declarations))
    return NULL;
  psx_statement_syntax_context_t syntax =
      psx_legacy_statement_syntax_context(&adapter);
  return psx_stmt_stmt_syntax(&syntax);
}

node_t *psx_parse_statement_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  psx_legacy_statement_syntax_adapter_t adapter;
  if (!psx_legacy_statement_syntax_adapter_init(
          &adapter, semantic_context, global_registry, local_registry,
          runtime_context, name_classifier, local_declarations))
    return NULL;
  psx_statement_syntax_context_t syntax =
      psx_legacy_statement_syntax_context(&adapter);
  return psx_parse_statement_expression_syntax(&syntax);
}
