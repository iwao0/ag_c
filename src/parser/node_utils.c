#include "internal/node_utils.h"
#include "internal/arena.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

int psx_node_type_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.type_size;
    case ND_GVAR: return as_mem(node)->type_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
      return as_mem(node)->type_size;
    case ND_COMMA:
      return psx_node_type_size(node->rhs);
    default:
      return 0;
  }
}

int psx_node_deref_size(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.deref_size;
    case ND_GVAR: return as_mem(node)->deref_size;
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
      return as_mem(node)->deref_size;
    case ND_COMMA:
      return psx_node_deref_size(node->rhs);
    default:
      return 0;
  }
}

int psx_node_is_pointer(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.is_pointer;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING:
      return as_mem(node)->is_pointer;
    case ND_COMMA:
      return psx_node_is_pointer(node->rhs);
    default:
      return 0;
  }
}

void psx_node_get_tag_type(node_t *node, token_kind_t *tag_kind, char **tag_name, int *tag_len, int *is_tag_pointer) {
  token_kind_t kind = TK_EOF;
  char *name = NULL;
  int len = 0;
  int ptr = 0;
  if (node) {
    switch (node->kind) {
      case ND_LVAR:
        kind = as_lvar(node)->mem.tag_kind;
        name = as_lvar(node)->mem.tag_name;
        len = as_lvar(node)->mem.tag_len;
        ptr = as_lvar(node)->mem.is_tag_pointer;
        break;
      case ND_GVAR:
      case ND_DEREF:
      case ND_ASSIGN:
      case ND_ADDR:
      case ND_STRING:
        kind = as_mem(node)->tag_kind;
        name = as_mem(node)->tag_name;
        len = as_mem(node)->tag_len;
        ptr = as_mem(node)->is_tag_pointer;
        break;
      case ND_COMMA:
        psx_node_get_tag_type(node->rhs, &kind, &name, &len, &ptr);
        break;
      default:
        break;
    }
  }
  if (tag_kind) *tag_kind = kind;
  if (tag_name) *tag_name = name;
  if (tag_len) *tag_len = len;
  if (is_tag_pointer) *is_tag_pointer = ptr;
}

static int node_is_unsigned(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR: return as_lvar(node)->mem.is_unsigned;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
      return as_mem(node)->is_unsigned;
    default: return node->is_unsigned;
  }
}

node_t *psx_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = arena_alloc(sizeof(node_t));
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
  // unsigned伝播: どちらかがunsignedなら結果もunsigned
  if (node_is_unsigned(lhs) || node_is_unsigned(rhs)) {
    node->is_unsigned = 1;
  }
  // _Complex伝播: どちらかが_Complexなら結果も_Complex
  if ((lhs && lhs->is_complex) || (rhs && rhs->is_complex)) {
    node->is_complex = 1;
  }
  return node;
}

node_t *psx_node_new_num(long long val) {
  node_num_t *node = arena_alloc(sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->val = val;
  return (node_t *)node;
}

node_t *psx_node_new_lvar(int offset) {
  node_lvar_t *node = arena_alloc(sizeof(node_lvar_t));
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
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_ASSIGN;
  node->base.lhs = lhs;
  node->base.rhs = rhs;
  node->base.fp_kind = lhs ? lhs->fp_kind : TK_FLOAT_KIND_NONE;
  if (lhs && lhs->is_complex) {
    node->base.is_complex = 1;
  }
  if (lhs && lhs->is_atomic) {
    node->base.is_atomic = 1;
  }
  return node;
}

void psx_node_reject_const_assign(node_t *node, const char *op) {
  if (!node) return;
  if (node->kind == ND_LVAR || node->kind == ND_GVAR) {
    node_mem_t *mem = as_mem(node);
    if (mem->is_const_qualified) {
      diag_emit_tokf(DIAG_ERR_PARSER_CONST_ASSIGNMENT, token,
                     diag_message_for(DIAG_ERR_PARSER_CONST_ASSIGNMENT));
    }
  }
}

static int node_pointee_is_const(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_LVAR:
    case ND_GVAR:
    case ND_DEREF:
    case ND_ASSIGN:
    case ND_ADDR:
    case ND_STRING: {
      node_mem_t *m = (node_mem_t *)node;
      return m->is_tag_pointer && m->is_const_qualified;
    }
    case ND_COMMA:
      return node_pointee_is_const(node->rhs);
    default:
      return 0;
  }
}

void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs) return;
  if (lhs->kind != ND_LVAR && lhs->kind != ND_GVAR) return;
  node_mem_t *lhs_mem = as_mem(lhs);
  if (!lhs_mem->is_tag_pointer) return;
  if (lhs_mem->is_const_qualified) return;
  if (node_pointee_is_const(rhs)) {
    diag_emit_tokf(DIAG_ERR_PARSER_CONST_QUAL_DISCARD, token,
                   diag_message_for(DIAG_ERR_PARSER_CONST_QUAL_DISCARD));
  }
}

void psx_node_expect_lvalue(node_t *node, const char *op) {
  if (!node || (node->kind != ND_LVAR && node->kind != ND_DEREF && node->kind != ND_GVAR)) {
    diag_emit_tokf(DIAG_ERR_PARSER_LVALUE_REQUIRED, token,
                   diag_message_for(DIAG_ERR_PARSER_LVALUE_REQUIRED), (char *)op);
  }
}

void psx_node_expect_incdec_target(node_t *node, const char *op) {
  psx_node_expect_lvalue(node, op);
  psx_node_reject_const_assign(node, op);
  if (node->fp_kind != TK_FLOAT_KIND_NONE) {
    diag_emit_tokf(DIAG_ERR_PARSER_INTEGER_SCALAR_REQUIRED, token,
                   diag_message_for(DIAG_ERR_PARSER_INTEGER_SCALAR_REQUIRED), (char *)op);
  }
}

node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op) {
  psx_node_expect_lvalue(lhs, op);
  psx_node_reject_const_assign(lhs, op);
  node_t *op_expr = psx_node_new_binary(op_kind, lhs, rhs);
  node_mem_t *assign_node = psx_node_new_assign(lhs, op_expr);
  assign_node->type_size = psx_node_type_size(lhs);
  assign_node->base.fp_kind = lhs ? lhs->fp_kind : 0;
  return (node_t *)assign_node;
}
