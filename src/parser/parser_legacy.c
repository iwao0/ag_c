#include "parser_legacy.h"

#include "alignas_value.h"
#include "core.h"
#include "decl.h"
#include "expr.h"
#include "name_classifier_legacy.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "../tokenizer/tokenizer.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  tokenizer_context_t *tokenizer_context;
} legacy_type_spec_syntax_context_t;

static void consume_legacy_alignas(
    void *opaque, psx_type_spec_result_t *result) {
  legacy_type_spec_syntax_context_t *context = opaque;
  tk_set_current_token_ctx(
      context->tokenizer_context,
      tk_get_current_token_ctx(context->tokenizer_context)->next);
  int value = psx_parse_alignas_value_in_contexts(
      context->semantic_context, NULL, context->tokenizer_context);
  if (value > result->alignas_value) result->alignas_value = value;
}

token_kind_t psx_consume_type_kind_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context,
    psx_type_spec_result_t *out) {
  if (!semantic_context || !tokenizer_context) return TK_EOF;
  legacy_type_spec_syntax_context_t context = {
      .semantic_context = semantic_context,
      .tokenizer_context = tokenizer_context,
  };
  psx_type_spec_syntax_t syntax = {
      .diagnostics = ps_ctx_diagnostics(semantic_context),
      .tokenizer_context = tokenizer_context,
      .consume_alignas_context = &context,
      .consume_alignas = consume_legacy_alignas,
  };
  return psx_consume_type_kind_with_syntax_ex(out, &syntax);
}

static node_t *parse_expression_in_legacy_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    node_t *(*parse)(
        psx_parser_runtime_context_t *,
        const psx_name_classifier_t *,
        const psx_local_declaration_callbacks_t *,
        char *, int)) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context || !parse)
    return NULL;
  char *function_name = NULL;
  int function_name_len = 0;
  ps_decl_get_current_funcname_in(
      local_registry, &function_name, &function_name_len);
  psx_name_classifier_t semantic_classifier =
      ps_ctx_name_classifier(semantic_context);
  const psx_name_classifier_t *source_classifier =
      name_classifier ? name_classifier : &semantic_classifier;
  psx_legacy_name_classifier_t classifier_adapter;
  if (!psx_legacy_name_classifier_init(
          &classifier_adapter, source_classifier, local_registry))
    return NULL;
  psx_name_classifier_t classifier =
      psx_legacy_name_classifier_view(&classifier_adapter);
  return parse(
      runtime_context, &classifier, local_declarations,
      function_name, function_name_len);
}

node_t *psx_expr_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  return parse_expression_in_legacy_contexts(
      semantic_context, global_registry, local_registry,
      runtime_context, name_classifier, local_declarations,
      psx_expr_expr_with_syntax_services);
}

node_t *psx_expr_assign_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  return parse_expression_in_legacy_contexts(
      semantic_context, global_registry, local_registry,
      runtime_context, name_classifier, local_declarations,
      psx_expr_assign_with_syntax_services);
}

node_t *psx_expr_conditional_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  return parse_expression_in_legacy_contexts(
      semantic_context, global_registry, local_registry,
      runtime_context, name_classifier, local_declarations,
      psx_expr_conditional_with_syntax_services);
}

node_t *ps_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations,
    tokenizer_context_t *tk_ctx, token_t *start) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  tokenizer_context_t *runtime_tokenizer =
      tk_ctx ? tk_ctx : ps_parser_runtime_tokenizer(runtime_context);
  if (!runtime_tokenizer) return NULL;
  tokenizer_context_t *previous_runtime_tokenizer =
      ps_parser_runtime_bind_tokenizer(runtime_context, runtime_tokenizer);
  tk_set_current_token_ctx(runtime_tokenizer, start);
  node_t *node = psx_expr_expr_in_contexts(
      semantic_context, global_registry, local_registry,
      runtime_context,
      local_declarations ? &local_declarations->name_classifier : NULL,
      local_declarations);
  if (previous_runtime_tokenizer) {
    ps_parser_runtime_bind_tokenizer(
        runtime_context, previous_runtime_tokenizer);
  }
  return node;
}
