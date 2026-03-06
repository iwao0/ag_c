#include "parser.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
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
static node_t *funcdef(void);

// program = funcdef*
void program(void) {
  int i = 0;
  while (!at_eof()) {
    code[i++] = funcdef();
  }
  code[i] = NULL;
}

// funcdef = ident "(" params? ")" "{" stmt* "}"
// params  = ident ("," ident)*
static node_t *funcdef(void) {
  token_t *tok = consume_ident();
  if (!tok) {
    fprintf(stderr, "関数定義が期待されます\n");
    exit(1);
  }
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = ND_FUNCDEF;
  node->funcname = tok->str;
  node->funcname_len = tok->len;

  expect('(');
  // 仮引数のパース
  int nargs = 0;
  if (!consume(')')) {
    token_t *param = consume_ident();
    if (param) {
      node->args[nargs++] = new_node_lvar((param->str[0] - 'a' + 1) * 8);
    }
    while (consume(',')) {
      param = consume_ident();
      if (param) {
        node->args[nargs++] = new_node_lvar((param->str[0] - 'a' + 1) * 8);
      }
    }
    expect(')');
  }
  node->nargs = nargs;

  // 関数本体 (ブロック)
  expect('{');
  node_t *body = calloc(1, sizeof(node_t));
  body->kind = ND_BLOCK;
  int i = 0;
  while (!consume('}')) {
    body->body[i++] = stmt();
  }
  body->body[i] = NULL;
  node->rhs = body;

  return node;
}

// stmt = "{" stmt* "}"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "while" "(" expr ")" stmt
//      | "for" "(" expr? ";" expr? ";" expr? ")" stmt
//      | "return" expr ";"
//      | expr ";"
static node_t *stmt(void) {
  if (consume('{')) {
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_BLOCK;
    int i = 0;
    while (!consume('}')) {
      node->body[i++] = stmt();
    }
    node->body[i] = NULL;
    return node;
  }

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

// primary = ident "(" args? ")" | "(" expr ")" | ident | num
// args    = expr ("," expr)*
static node_t *primary(void) {
  if (consume('(')) {
    node_t *node = expr();
    expect(')');
    return node;
  }

  token_t *tok = consume_ident();
  if (tok) {
    // 関数呼び出し: ident "(" args? ")"
    if (consume('(')) {
      node_t *node = calloc(1, sizeof(node_t));
      node->kind = ND_FUNCALL;
      node->funcname = tok->str;
      node->funcname_len = tok->len;
      int nargs = 0;
      if (!consume(')')) {
        node->args[nargs++] = expr();
        while (consume(',')) {
          node->args[nargs++] = expr();
        }
        expect(')');
      }
      node->nargs = nargs;
      return node;
    }
    // ローカル変数
    int offset = (tok->str[0] - 'a' + 1) * 8;
    return new_node_lvar(offset);
  }

  return new_node_num(expect_number());
}
