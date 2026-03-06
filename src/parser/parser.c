#include "parser.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

static node_t *new_node(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static node_t *new_node_num(int val) {
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = ND_NUM;
  node->val = val;
  return node;
}

static node_t *mul(void);
static node_t *primary(void);

// expr = mul ("+" mul | "-" mul)*
node_t *expr(void) {
  node_t *node = mul();

  for (;;) {
    if (consume('+'))
      node = new_node(ND_ADD, node, mul());
    else if (consume('-'))
      node = new_node(ND_SUB, node, mul());
    else
      return node;
  }
}

// mul = primary ("*" primary | "/" primary)*
static node_t *mul(void) {
  node_t *node = primary();

  for (;;) {
    if (consume('*'))
      node = new_node(ND_MUL, node, primary());
    else if (consume('/'))
      node = new_node(ND_DIV, node, primary());
    else
      return node;
  }
}

// primary = "(" expr ")" | num
static node_t *primary(void) {
  if (consume('(')) {
    node_t *node = expr();
    expect(')');
    return node;
  }
  return new_node_num(expect_number());
}
