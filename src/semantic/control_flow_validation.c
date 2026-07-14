#include "control_flow_validation.h"

#include "../diag/diag.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/dynarray.h"

#include <stdlib.h>

typedef struct {
  long long *case_values;
  int count;
  int capacity;
  int has_default;
} switch_label_context_t;

static void validate_node(
    node_t *node, const token_t *fallback, int loop_depth,
    int switch_depth);

static void validate_array(
    node_t **nodes, const token_t *fallback, int loop_depth,
    int switch_depth) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    validate_node(nodes[i], fallback, loop_depth, switch_depth);
}

static void switch_context_dispose(switch_label_context_t *context) {
  if (!context) return;
  free(context->case_values);
  context->case_values = NULL;
  context->count = 0;
  context->capacity = 0;
  context->has_default = 0;
}

static void register_case(
    switch_label_context_t *context, node_case_t *case_node,
    const token_t *fallback) {
  const token_t *tok = case_node->base.tok
                           ? case_node->base.tok
                           : fallback;
  for (int i = 0; i < context->count; i++) {
    if (context->case_values[i] == case_node->val) {
      diag_emit_tokf(
          DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE, tok,
          diag_message_for(DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE),
          case_node->val);
    }
  }
  if (context->count >= context->capacity) {
    context->capacity =
        pda_next_cap(context->capacity, context->count + 1);
    context->case_values = pda_xreallocarray(
        context->case_values, (size_t)context->capacity,
        sizeof(long long));
  }
  context->case_values[context->count++] = case_node->val;
}

static void register_default(
    switch_label_context_t *context, node_t *default_node,
    const token_t *fallback) {
  const token_t *tok = default_node->tok ? default_node->tok : fallback;
  if (context->has_default) {
    diag_emit_tokf(
        DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT, tok, "%s",
        diag_message_for(DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT));
  }
  context->has_default = 1;
}

static void collect_switch_labels(
    node_t *node, switch_label_context_t *context,
    const token_t *fallback);

static void collect_switch_label_array(
    node_t **nodes, switch_label_context_t *context,
    const token_t *fallback) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    collect_switch_labels(nodes[i], context, fallback);
}

static void collect_switch_labels(
    node_t *node, switch_label_context_t *context,
    const token_t *fallback) {
  if (!node) return;
  switch (node->kind) {
    case ND_SWITCH:
      return;
    case ND_CASE:
      register_case(context, (node_case_t *)node, fallback);
      collect_switch_labels(node->rhs, context, fallback);
      return;
    case ND_DEFAULT:
      register_default(context, node, fallback);
      collect_switch_labels(node->rhs, context, fallback);
      return;
    case ND_BLOCK:
      collect_switch_label_array(
          ((node_block_t *)node)->body, context, fallback);
      return;
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      collect_switch_labels(call->callee, context, fallback);
      collect_switch_label_array(call->arguments, context, fallback);
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      collect_switch_labels(control->init, context, fallback);
      collect_switch_labels(node->lhs, context, fallback);
      collect_switch_labels(node->rhs, context, fallback);
      collect_switch_labels(control->inc, context, fallback);
      collect_switch_labels(control->els, context, fallback);
      return;
    }
    default:
      collect_switch_labels(node->lhs, context, fallback);
      collect_switch_labels(node->rhs, context, fallback);
      return;
  }
}

static void validate_switch_labels(node_t *node, const token_t *fallback) {
  if (!node || node->kind != ND_SWITCH) return;
  switch_label_context_t context = {0};
  collect_switch_labels(node->rhs, &context, fallback);
  switch_context_dispose(&context);
}

static void validate_node(
    node_t *node, const token_t *fallback, int loop_depth,
    int switch_depth) {
  if (!node) return;
  const token_t *tok = node->tok ? node->tok : fallback;
  switch (node->kind) {
    case ND_BREAK:
      if (loop_depth == 0 && switch_depth == 0)
        ps_diag_only_in(
            (token_t *)tok, diag_text_for(DIAG_TEXT_BREAK),
            diag_text_for(DIAG_TEXT_LOOP_OR_SWITCH_SCOPE));
      return;
    case ND_CONTINUE:
      if (loop_depth == 0)
        ps_diag_only_in(
            (token_t *)tok, diag_text_for(DIAG_TEXT_CONTINUE),
            diag_text_for(DIAG_TEXT_LOOP_SCOPE));
      return;
    case ND_BLOCK:
      validate_array(
          ((node_block_t *)node)->body, fallback,
          loop_depth, switch_depth);
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      validate_array(
          function->parameters, fallback, loop_depth, switch_depth);
      validate_node(node->rhs, fallback, loop_depth, switch_depth);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      validate_node(call->callee, fallback, loop_depth, switch_depth);
      for (int i = 0; i < call->argument_count; i++)
        validate_node(
            call->arguments[i], fallback, loop_depth, switch_depth);
      return;
    }
    case ND_WHILE:
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      validate_node(node->rhs, fallback, loop_depth + 1, switch_depth);
      return;
    case ND_DO_WHILE:
      validate_node(node->rhs, fallback, loop_depth + 1, switch_depth);
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      return;
    case ND_FOR: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      validate_node(control->init, fallback, loop_depth, switch_depth);
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      validate_node(control->inc, fallback, loop_depth, switch_depth);
      validate_node(node->rhs, fallback, loop_depth + 1, switch_depth);
      return;
    }
    case ND_SWITCH:
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      validate_switch_labels(node, fallback);
      validate_node(node->rhs, fallback, loop_depth, switch_depth + 1);
      return;
    case ND_CASE:
      if (switch_depth == 0)
        ps_diag_only_in(
            (token_t *)tok, diag_text_for(DIAG_TEXT_CASE),
            diag_text_for(DIAG_TEXT_SWITCH_SCOPE));
      validate_node(node->rhs, fallback, loop_depth, switch_depth);
      return;
    case ND_DEFAULT:
      if (switch_depth == 0)
        ps_diag_only_in(
            (token_t *)tok, diag_text_for(DIAG_TEXT_DEFAULT),
            diag_text_for(DIAG_TEXT_SWITCH_SCOPE));
      validate_node(node->rhs, fallback, loop_depth, switch_depth);
      return;
    case ND_LABEL:
      validate_node(node->rhs, fallback, loop_depth, switch_depth);
      return;
    case ND_IF:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      validate_node(node->rhs, fallback, loop_depth, switch_depth);
      validate_node(control->els, fallback, loop_depth, switch_depth);
      return;
    }
    case ND_STMT_EXPR:
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      return;
    default:
      validate_node(node->lhs, fallback, loop_depth, switch_depth);
      validate_node(node->rhs, fallback, loop_depth, switch_depth);
      return;
  }
}

void psx_validate_control_flow(
    node_t *node, const token_t *fallback_diag_tok) {
  validate_node(node, fallback_diag_tok, 0, 0);
}

static int statement_tail_terminates(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_RETURN:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
      return 1;
    case ND_CASE:
    case ND_DEFAULT:
      return statement_tail_terminates(node->rhs);
    case ND_BLOCK: {
      node_t *last = NULL;
      node_t **body = ((node_block_t *)node)->body;
      for (int i = 0; body && body[i]; i++) last = body[i];
      return statement_tail_terminates(last);
    }
    default:
      return 0;
  }
}

static int statement_direct_terminates(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_RETURN:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
      return 1;
    case ND_CASE:
    case ND_DEFAULT:
      return statement_tail_terminates(node);
    default:
      return 0;
  }
}

static int resumes_reachable(node_t *node) {
  return node && (node->kind == ND_CASE || node->kind == ND_DEFAULT ||
                  node->kind == ND_LABEL);
}

static int is_switch_label(node_t *node) {
  return node && (node->kind == ND_CASE || node->kind == ND_DEFAULT);
}

static void suppress_lvar_regions(node_t *node) {
  if (!node) return;
  ps_decl_suppress_lvar_usage_region(node->usage_region);
  switch (node->kind) {
    case ND_BLOCK:
      for (node_t **body = ((node_block_t *)node)->body;
           body && *body; body++)
        suppress_lvar_regions(*body);
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        suppress_lvar_regions(function->parameters[i]);
      suppress_lvar_regions(node->rhs);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      suppress_lvar_regions(call->callee);
      for (int i = 0; i < call->argument_count; i++)
        suppress_lvar_regions(call->arguments[i]);
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      suppress_lvar_regions(control->init);
      suppress_lvar_regions(node->lhs);
      suppress_lvar_regions(node->rhs);
      suppress_lvar_regions(control->inc);
      suppress_lvar_regions(control->els);
      return;
    }
    default:
      suppress_lvar_regions(node->lhs);
      suppress_lvar_regions(node->rhs);
      return;
  }
}

static void emit_unreachable_node(node_t *node, const token_t *fallback);

static void emit_unreachable_block(
    node_block_t *block, const token_t *fallback) {
  if (!block || !block->body) return;
  int previous_terminates = 0;
  int seen_case = 0;
  int previous_fallthrough_terminates = 0;
  int in_unreachable_run = 0;
  for (int i = 0; block->body[i]; i++) {
    node_t *statement = block->body[i];
    if (seen_case && !previous_fallthrough_terminates &&
        is_switch_label(statement)) {
      diag_warn_tokf(
          DIAG_WARN_PARSER_SWITCH_FALLTHROUGH,
          statement->tok ? statement->tok : fallback, "%s",
          diag_warn_message_for(DIAG_WARN_PARSER_SWITCH_FALLTHROUGH));
    }
    int resumes = resumes_reachable(statement);
    if (resumes) in_unreachable_run = 0;
    if (previous_terminates && !resumes && !in_unreachable_run) {
      diag_warn_tokf(
          DIAG_WARN_PARSER_UNREACHABLE_CODE,
          statement->tok ? statement->tok : fallback, "%s",
          diag_warn_message_for(DIAG_WARN_PARSER_UNREACHABLE_CODE));
      in_unreachable_run = 1;
    }
    if (in_unreachable_run) suppress_lvar_regions(statement);
    emit_unreachable_node(statement, fallback);
    previous_terminates = statement_direct_terminates(statement);
    previous_fallthrough_terminates = previous_terminates;
    if (is_switch_label(statement)) seen_case = 1;
  }
}

static void emit_unreachable_node(node_t *node, const token_t *fallback) {
  if (!node) return;
  switch (node->kind) {
    case ND_BLOCK:
      emit_unreachable_block((node_block_t *)node, fallback);
      return;
    case ND_FUNCDEF:
      emit_unreachable_node(node->rhs, fallback);
      return;
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      emit_unreachable_node(call->callee, fallback);
      for (int i = 0; i < call->argument_count; i++)
        emit_unreachable_node(call->arguments[i], fallback);
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      emit_unreachable_node(control->init, fallback);
      emit_unreachable_node(node->lhs, fallback);
      emit_unreachable_node(node->rhs, fallback);
      emit_unreachable_node(control->inc, fallback);
      emit_unreachable_node(control->els, fallback);
      return;
    }
    default:
      emit_unreachable_node(node->lhs, fallback);
      emit_unreachable_node(node->rhs, fallback);
      return;
  }
}

void psx_emit_unreachable_warnings(
    node_t *node, const token_t *fallback_diag_tok) {
  emit_unreachable_node(node, fallback_diag_tok);
}
