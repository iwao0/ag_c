#include "parser.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

node_t *code[MAX_STMTS];

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

static node_t *new_node_lvar(int offset) {
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = ND_LVAR;
  node->offset = offset;
  return node;
}

static node_t *stmt(void);
static node_t *assign(void);
static node_t *equality(void);
static node_t *relational(void);
static node_t *add(void);
static node_t *mul(void);
static node_t *primary(void);

// program = stmt*
void program(void) {
  int i = 0;
  while (!at_eof()) {
    code[i++] = stmt();
  }
  code[i] = NULL;
}

// stmt = "if" "(" expr ")" stmt ("else" stmt)?
//      | "while" "(" expr ")" stmt
//      | "for" "(" expr? ";" expr? ";" expr? ")" stmt
//      | "return" expr ";"
//      | expr ";"
static node_t *stmt(void) {
  if (token->kind == TK_RETURN) {
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_RETURN;
    node->lhs = expr();
    expect(';');
    return node;
  }

  if (token->kind == TK_IF) {
    token = token->next;
    expect('(');
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_IF;
    node->lhs = expr();
    expect(')');
    node->rhs = stmt();
    if (token->kind == TK_ELSE) {
      token = token->next;
      node->els = stmt();
    }
    return node;
  }

  if (token->kind == TK_WHILE) {
    token = token->next;
    expect('(');
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_WHILE;
    node->lhs = expr();  // 条件式
    expect(')');
    node->rhs = stmt();  // ループ本体
    return node;
  }

  if (token->kind == TK_FOR) {
    token = token->next;
    expect('(');
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_FOR;
    if (!consume(';')) {
      node->init = expr();  // 初期化式
      expect(';');
    }
    if (!consume(';')) {
      node->lhs = expr();   // 条件式
      expect(';');
    }
    if (!consume(')')) {
      node->inc = expr();   // インクリメント式
      expect(')');
    }
    node->rhs = stmt();     // ループ本体
    return node;
  }

  node_t *node = expr();
  expect(';');
  return node;
}

// expr = assign
node_t *expr(void) { return assign(); }

// assign = equality ("=" assign)?
static node_t *assign(void) {
  node_t *node = equality();
  if (consume('='))
    node = new_node(ND_ASSIGN, node, assign());
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
static node_t *equality(void) {
  node_t *node = relational();

  for (;;) {
    if (consume_str("=="))
      node = new_node(ND_EQ, node, relational());
    else if (consume_str("!="))
      node = new_node(ND_NE, node, relational());
    else
      return node;
  }
}

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static node_t *relational(void) {
  node_t *node = add();

  for (;;) {
    if (consume_str("<"))
      node = new_node(ND_LT, node, add());
    else if (consume_str("<="))
      node = new_node(ND_LE, node, add());
    else if (consume_str(">"))
      node = new_node(ND_LT, add(), node);
    else if (consume_str(">="))
      node = new_node(ND_LE, add(), node);
    else
      return node;
  }
}

// add = mul ("+" mul | "-" mul)*
static node_t *add(void) {
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

// primary = "(" expr ")" | ident | num
static node_t *primary(void) {
  if (consume('(')) {
    node_t *node = expr();
    expect(')');
    return node;
  }

  token_t *tok = consume_ident();
  if (tok) {
    // 変数名(1文字)からオフセットを計算: a=8, b=16, c=24, ...
    int offset = (tok->str[0] - 'a' + 1) * 8;
    return new_node_lvar(offset);
  }

  return new_node_num(expect_number());
}
