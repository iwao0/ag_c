#include "parser.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

node_t *code[MAX_STMTS];

// ローカル変数テーブル（関数ごとにリセット）
static lvar_t *locals;
static int locals_offset;

// 名前でローカル変数を検索
static lvar_t *find_lvar(char *name, int len) {
  for (lvar_t *var = locals; var; var = var->next) {
    if (var->len == len && memcmp(var->name, name, len) == 0) {
      return var;
    }
  }
  return NULL;
}

// 新しいローカル変数を登録（サイズ指定付き）
static lvar_t *register_lvar_sized(char *name, int len, int size, int is_array) {
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->name = name;
  var->len = len;
  locals_offset += size;
  var->offset = locals_offset;
  var->size = size;
  var->is_array = is_array;
  locals = var;
  return var;
}

static lvar_t *register_lvar(char *name, int len) {
  return register_lvar_sized(name, len, 8, 0);
}

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
static node_t *unary(void);
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

// consume_type: 型キーワード(int)があれば読み飛ばす（現在は無視）
static bool consume_type(void) {
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID || token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT || token->kind == TK_DOUBLE) {
    token = token->next;
    return true;
  }
  return false;
}

// funcdef = "int"? ident "(" params? ")" "{" stmt* "}"
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  consume_type(); // 戻り値の型（省略可）
  token_t *tok = consume_ident();
  if (!tok) {
    fprintf(stderr, "関数定義が期待されます\n");
    exit(1);
  }
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = ND_FUNCDEF;
  node->funcname = tok->str;
  node->funcname_len = tok->len;

  // 関数ごとにローカル変数テーブルをリセット
  locals = NULL;
  locals_offset = 0;

  expect('(');
  // 仮引数のパース
  int nargs = 0;
  if (!consume(')')) {
    consume_type(); // 仮引数の型
    while (consume('*')) {} // ポインタの * を読み飛ばす
    token_t *param = consume_ident();
    if (param) {
      lvar_t *var = register_lvar(param->str, param->len);
      node->args[nargs++] = new_node_lvar(var->offset);
    }
    while (consume(',')) {
      consume_type(); // 仮引数の型
      while (consume('*')) {} // ポインタの * を読み飛ばす
      param = consume_ident();
      if (param) {
        lvar_t *var = register_lvar(param->str, param->len);
        node->args[nargs++] = new_node_lvar(var->offset);
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
//      | "int" ident ("=" expr)? ";"
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

  // 型付き変数宣言: type "*"* ident ("[" num "]")? ("=" expr)? ";"
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID || token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT || token->kind == TK_DOUBLE) {
    token = token->next;
    // ポインタの * を読み飛ばす
    while (consume('*')) {}
    token_t *tok = consume_ident();
    if (!tok) {
      fprintf(stderr, "変数名が期待されます\n");
      exit(1);
    }
    lvar_t *var = find_lvar(tok->str, tok->len);
    if (!var) {
      // 配列宣言: ident "[" num "]"
      if (consume('[')) {
        int array_size = expect_number();
        expect(']');
        var = register_lvar_sized(tok->str, tok->len, array_size * 8, 1);
        if (consume('=')) {
          // 配列初期化は未対応
          expr();
        }
        expect(';');
        node_t *node = new_node_num(0);
        return node;
      }
      var = register_lvar(tok->str, tok->len);
    }
    if (consume('=')) {
      // int x = expr;
      node_t *node = new_node(ND_ASSIGN, new_node_lvar(var->offset), expr());
      expect(';');
      return node;
    }
    // int x; (初期化なし → ダミーの値0)
    expect(';');
    node_t *node = new_node_num(0);
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

// mul = unary ("*" unary | "/" unary)*
static node_t *mul(void) {
  node_t *node = unary();

  for (;;) {
    if (consume('*'))
      node = new_node(ND_MUL, node, unary());
    else if (consume('/'))
      node = new_node(ND_DIV, node, unary());
    else
      return node;
  }
}

// unary = ("*" | "&") unary | primary postfix*
// postfix = "[" expr "]"
static node_t *unary(void) {
  if (consume('*')) {
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_DEREF;
    node->lhs = unary();
    return node;
  }
  if (consume('&')) {
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_ADDR;
    node->lhs = unary();
    return node;
  }
  node_t *node = primary();
  // 後置演算子: [expr]
  while (consume('[')) {
    // arr[i] → *(arr + i*8)
    node_t *idx = expr();
    expect(']');
    // i * 8
    node_t *scaled = new_node(ND_MUL, idx, new_node_num(8));
    // arr + i*8
    node_t *addr = new_node(ND_ADD, node, scaled);
    // *(arr + i*8)
    node_t *deref = calloc(1, sizeof(node_t));
    deref->kind = ND_DEREF;
    deref->lhs = addr;
    node = deref;
  }
  return node;
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
    lvar_t *var = find_lvar(tok->str, tok->len);
    if (!var) {
      var = register_lvar(tok->str, tok->len);
    }
    if (var->is_array) {
      // 配列名は先頭要素のアドレスを返す
      // 配列の先頭は offset - size + 8 の位置
      node_t *node = calloc(1, sizeof(node_t));
      node->kind = ND_ADDR;
      node->lhs = new_node_lvar(var->offset - var->size + 8);
      return node;
    }
    return new_node_lvar(var->offset);
  }

  return new_node_num(expect_number());
}
