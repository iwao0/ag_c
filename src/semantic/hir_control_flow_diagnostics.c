#include "hir_control_flow_diagnostics.h"

#include "../diag/diag.h"

static int is_switch_label(const psx_hir_node_t *node) {
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  return kind == PSX_HIR_CASE || kind == PSX_HIR_DEFAULT;
}

static int statement_tail_terminates(
    const psx_hir_module_t *module, const psx_hir_node_t *node) {
  if (!module || !node) return 0;
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_RETURN:
    case PSX_HIR_BREAK:
    case PSX_HIR_CONTINUE:
    case PSX_HIR_GOTO:
      return 1;
    case PSX_HIR_CASE:
    case PSX_HIR_DEFAULT:
    case PSX_HIR_LABEL: {
      size_t count = psx_hir_node_child_count(node);
      const psx_hir_node_t *body = count > 0
          ? psx_hir_module_lookup(
                module, psx_hir_node_child_at(node, count - 1))
          : NULL;
      return statement_tail_terminates(module, body);
    }
    default:
      return 0;
  }
}

static void emit_node_warnings(
    const psx_hir_module_t *module, const psx_hir_node_t *node,
    ag_diagnostic_context_t *diagnostics,
    const token_t *fallback_diag_tok) {
  if (!module || !node) return;
  if (psx_hir_node_kind(node) == PSX_HIR_BLOCK) {
    int seen_case = 0;
    int previous_terminates = 0;
    for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
      const psx_hir_node_t *statement = psx_hir_module_lookup(
          module, psx_hir_node_child_at(node, i));
      if (seen_case && !previous_terminates &&
          is_switch_label(statement)) {
        diag_warn_tokf_in(
            diagnostics, DIAG_WARN_PARSER_SWITCH_FALLTHROUGH,
            fallback_diag_tok, "%s",
            diag_warn_message_for_in(
                diagnostics,
                DIAG_WARN_PARSER_SWITCH_FALLTHROUGH));
      }
      previous_terminates =
          statement_tail_terminates(module, statement);
      if (is_switch_label(statement)) seen_case = 1;
    }
  }
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    emit_node_warnings(
        module,
        psx_hir_module_lookup(module, psx_hir_node_child_at(node, i)),
        diagnostics, fallback_diag_tok);
  }
}

void psx_emit_hir_control_flow_warnings(
    const psx_hir_module_t *module, psx_hir_node_id_t root,
    ag_diagnostic_context_t *diagnostics,
    const token_t *fallback_diag_tok) {
  emit_node_warnings(
      module, psx_hir_module_lookup(module, root), diagnostics,
      fallback_diag_tok);
}
