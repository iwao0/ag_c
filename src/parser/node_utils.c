#include "node_utils.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

int psx_node_type_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.type_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
      return as_mem(node)->type_size;
    default:
      return 0;
  }
}

int psx_node_deref_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.deref_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
      return as_mem(node)->deref_size;
    default:
      return 0;
  }
}

node_t *psx_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  if (lhs && lhs->fp_kind) node->fp_kind = lhs->fp_kind;
  if (rhs && rhs->fp_kind > node->fp_kind) node->fp_kind = rhs->fp_kind;

  if (kind == ND_EQ || kind == ND_NE || kind == ND_LT || kind == ND_LE ||
      kind == ND_LOGAND || kind == ND_LOGOR ||
      kind == ND_BITAND || kind == ND_BITXOR || kind == ND_BITOR ||
      kind == ND_SHL || kind == ND_SHR) {
    node->fp_kind = TK_FLOAT_KIND_NONE;
  }
  return node;
}

node_t *psx_node_new_num(long long val) {
  node_num_t *node = calloc(1, sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->val = val;
  return (node_t *)node;
}

node_t *psx_node_new_lvar(int offset) {
  node_lvar_t *node = calloc(1, sizeof(node_lvar_t));
  node->mem.base.kind = ND_LVAR;
  node->offset = offset;
  node->mem.type_size = 8;
  return (node_t *)node;
}

node_t *psx_node_new_lvar_typed(int offset, int type_size) {
  node_lvar_t *node = (node_lvar_t *)psx_node_new_lvar(offset);
  node->mem.type_size = type_size;
  return (node_t *)node;
}

node_mem_t *psx_node_new_assign(node_t *lhs, node_t *rhs) {
  node_mem_t *node = calloc(1, sizeof(node_mem_t));
  node->base.kind = ND_ASSIGN;
  node->base.lhs = lhs;
  node->base.rhs = rhs;
  node->base.fp_kind = lhs ? lhs->fp_kind : TK_FLOAT_KIND_NONE;
  return node;
}

void psx_node_expect_lvalue(node_t *node, const char *op) {
  if (!node || (node->kind != ND_LVAR && node->kind != ND_DEREF)) {
    tk_error_tok(token, "%s の対象は左辺値である必要があります", (char *)op);
  }
}

void psx_node_expect_incdec_target(node_t *node, const char *op) {
  psx_node_expect_lvalue(node, op);
  if (node->fp_kind != TK_FLOAT_KIND_NONE) {
    tk_error_tok(token, "%s の対象は整数スカラーである必要があります", (char *)op);
  }
}

node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op) {
  psx_node_expect_lvalue(lhs, op);
  node_t *op_expr = psx_node_new_binary(op_kind, lhs, rhs);
  node_mem_t *assign_node = psx_node_new_assign(lhs, op_expr);
  assign_node->type_size = psx_node_type_size(lhs);
  assign_node->base.fp_kind = lhs ? lhs->fp_kind : 0;
  return (node_t *)assign_node;
}

