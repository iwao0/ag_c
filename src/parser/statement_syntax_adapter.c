#include "stmt_legacy.h"

#include "decl.h"
#include "enum_const.h"
#include "expr.h"
#include "local_registry.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "stmt.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_parser_runtime_context_t *runtime_context;
  const psx_local_declaration_callbacks_t *local_declarations;
  psx_name_classifier_t name_classifier;
} psx_legacy_statement_syntax_adapter_t;

static node_t *parse_expression(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  return psx_expr_expr_in_contexts(
      adapter->semantic_context, adapter->global_registry,
      adapter->local_registry, adapter->runtime_context,
      &adapter->name_classifier, adapter->local_declarations);
}

static node_t *parse_local_declaration(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  return psx_parse_local_declaration_syntax(
      adapter->local_declarations);
}

static long long parse_case_constant(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  return psx_parse_case_const_expr_in_contexts(
      adapter->semantic_context, &adapter->name_classifier,
      ps_parser_runtime_tokenizer(adapter->runtime_context));
}

static void enter_block_scope(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  ps_ctx_enter_block_scope_in(adapter->semantic_context);
  ps_decl_enter_scope_in(adapter->local_registry);
}

static void leave_block_scope(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  ps_decl_leave_scope_in(adapter->local_registry);
  ps_ctx_leave_block_scope_in(adapter->semantic_context);
}

static void enter_local_scope(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  ps_decl_enter_scope_in(adapter->local_registry);
}

static void leave_local_scope(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  ps_decl_leave_scope_in(adapter->local_registry);
}

static psx_lvar_usage_region_t *begin_usage_region(void *context) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  return psx_decl_begin_lvar_usage_region_in(adapter->local_registry);
}

static void end_usage_region(
    void *context, psx_lvar_usage_region_t *region) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  psx_decl_end_lvar_usage_region_in(adapter->local_registry, region);
}

static void register_goto(
    void *context, char *name, int name_len, token_t *token) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  psx_ctx_register_goto_ref_in(
      adapter->semantic_context, name, name_len, token);
}

static void register_label(
    void *context, char *name, int name_len, token_t *token) {
  psx_legacy_statement_syntax_adapter_t *adapter = context;
  psx_ctx_register_label_def_in(
      adapter->semantic_context, name, name_len, token);
}

static psx_statement_syntax_context_t statement_syntax_context(
    psx_legacy_statement_syntax_adapter_t *adapter) {
  return (psx_statement_syntax_context_t){
      .context = adapter,
      .runtime_context = adapter->runtime_context,
      .name_classifier = adapter->name_classifier,
      .parse_expression = parse_expression,
      .parse_local_declaration = parse_local_declaration,
      .parse_case_constant = parse_case_constant,
      .enter_block_scope = enter_block_scope,
      .leave_block_scope = leave_block_scope,
      .enter_local_scope = enter_local_scope,
      .leave_local_scope = leave_local_scope,
      .begin_usage_region = begin_usage_region,
      .end_usage_region = end_usage_region,
      .register_goto = register_goto,
      .register_label = register_label,
  };
}

static int initialize_adapter(
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
  *adapter = (psx_legacy_statement_syntax_adapter_t){
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .local_declarations = local_declarations,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
  };
  return 1;
}

node_t *psx_stmt_stmt_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  psx_legacy_statement_syntax_adapter_t adapter;
  if (!initialize_adapter(
          &adapter, semantic_context, global_registry, local_registry,
          runtime_context, name_classifier, local_declarations))
    return NULL;
  psx_statement_syntax_context_t syntax =
      statement_syntax_context(&adapter);
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
  if (!initialize_adapter(
          &adapter, semantic_context, global_registry, local_registry,
          runtime_context, name_classifier, local_declarations))
    return NULL;
  psx_statement_syntax_context_t syntax =
      statement_syntax_context(&adapter);
  return psx_parse_statement_expression_syntax(&syntax);
}
