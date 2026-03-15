#include "parser.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

node_t **code;
string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;
static int string_label_count = 0;
static int float_label_count = 0;

// ローカル変数テーブル（連結リスト）
typedef struct lvar_t lvar_t;
struct lvar_t {
  lvar_t *next;
  char *name;
  int len;
  int offset;
  int size;      // サイズ（スカラー=8、配列=要素数*elem_size）
  int elem_size;   // 要素サイズ（1=char, 8=int/pointer）
  int is_array;  // 配列かどうか
  int is_float;  // 0=整数, 1=float, 2=double
};

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
static lvar_t *register_lvar_sized(char *name, int len, int size, int elem_size, int is_array) {
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->name = name;
  var->len = len;
  locals_offset += size;
  var->offset = locals_offset;
  var->size = size;
  var->elem_size = elem_size;
  var->is_array = is_array;
  locals = var;
  return var;
}

static lvar_t *register_lvar(char *name, int len) {
  return register_lvar_sized(name, len, 8, 8, 0);
}

static node_t *new_node(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  // 左辺と右辺から is_float を伝播 (double優先)
  if (lhs && lhs->is_float) node->is_float = lhs->is_float;
  if (rhs && rhs->is_float > node->is_float) node->is_float = rhs->is_float;
  
  // 比較演算の結果は整数(0 または 1)
  if (kind == ND_EQ || kind == ND_NE || kind == ND_LT || kind == ND_LE) {
    node->is_float = 0;
  }
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
  node->type_size = 8; // デフォルトは8バイト
  return node;
}

static node_t *new_node_lvar_typed(int offset, int type_size) {
  node_t *node = new_node_lvar(offset);
  node->type_size = type_size;
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
  int cap = 8;
  code = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!at_eof()) {
    if (i >= cap - 1) { // NULL終端用
      cap *= 2;
      code = realloc(code, sizeof(node_t*) * cap);
    }
    code[i++] = funcdef();
  }
  code[i] = NULL;
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
static token_kind_t consume_type_kind(void) {
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID || token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT || token->kind == TK_DOUBLE) {
    token_kind_t kind = token->kind;
    token = token->next;
    return kind;
  }
  return TK_EOF; // 型なし
}

static bool consume_type(void) {
  return consume_type_kind() != TK_EOF;
}

static int current_func_ret_type = 0; // 0=int, 1=float, 2=double

// funcdef = "int"? ident "(" params? ")" "{" stmt* "}"
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  token_kind_t ret_kind = consume_type_kind(); // 戻り値の型（省略可）
  current_func_ret_type = 0;
  if (ret_kind == TK_FLOAT) current_func_ret_type = 1;
  else if (ret_kind == TK_DOUBLE) current_func_ret_type = 2;
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
  int arg_cap = 4;
  node->args = calloc(arg_cap, sizeof(node_t*));
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
      if (nargs >= arg_cap) {
        arg_cap *= 2;
        node->args = realloc(node->args, sizeof(node_t*) * arg_cap);
      }
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
  int body_cap = 8;
  body->body = calloc(body_cap, sizeof(node_t*));
  while (!consume('}')) {
    if (i >= body_cap - 1) {
      body_cap *= 2;
      body->body = realloc(body->body, sizeof(node_t*) * body_cap);
    }
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
    int cap = 8;
    node->body = calloc(cap, sizeof(node_t*));
    while (!consume('}')) {
      if (i >= cap - 1) {
        cap *= 2;
        node->body = realloc(node->body, sizeof(node_t*) * cap);
      }
      node->body[i++] = stmt();
    }
    node->body[i] = NULL;
    return node;
  }

  // 型付き変数宣言: type "*"* ident ("[" num "]")? ("=" expr)? ";"
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID || token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT || token->kind == TK_DOUBLE) {
    token_kind_t type_kind = consume_type_kind();
    int elem_size = (type_kind == TK_CHAR) ? 1 : (type_kind == TK_SHORT) ? 2 : (type_kind == TK_INT || type_kind == TK_FLOAT) ? 4 : 8;
    // ポインタの * を読み飛ばす（ポインタ自体は8バイト）
    int is_pointer = 0;
    while (consume('*')) { is_pointer = 1; }
    int var_size = is_pointer ? 8 : elem_size;
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
        var = register_lvar_sized(tok->str, tok->len, array_size * elem_size, elem_size, 1);
        if (consume('=')) {
          // 配列初期化は未対応
          expr();
        }
        expect(';');
        node_t *node = new_node_num(0);
        return node;
      }
      var = register_lvar_sized(tok->str, tok->len, var_size, is_pointer ? elem_size : var_size, 0);
    }
    // float/double フラグを設定
    if (!is_pointer) {
      if (type_kind == TK_FLOAT) var->is_float = 1;
      else if (type_kind == TK_DOUBLE) var->is_float = 2;
    }
    if (consume('=')) {
      // int x = expr;
      node_t *lvar = new_node_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
      lvar->is_float = var->is_float;
      node_t *node = new_node(ND_ASSIGN, lvar, expr());
      node->type_size = is_pointer ? 8 : var->elem_size;
      node->is_float = var->is_float;
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
    node->is_float = current_func_ret_type; // 関数宣言時の戻り値型を記録
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
  if (consume('=')) {
    node = new_node(ND_ASSIGN, node, assign());
    // 左辺のtype_sizeをASSIGNに伝播（str/strb/strh の切り替えに使用）
    node->type_size = node->lhs->type_size;
    node->is_float = node->lhs->is_float;
  }
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
    node_t *operand = unary();
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_DEREF;
    node->lhs = operand;
    // デリファレンス結果のサイズ: オペランドが指す先の要素サイズ
    node->type_size = operand->deref_size ? operand->deref_size : 8;
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
    // arr[i] → *(arr + i*elem_size)
    node_t *idx = expr();
    expect(']');
    // 要素サイズを取得（nodeから伝播、デフォルトは8）
    // 要素サイズを取得（deref_size > type_size の優先度で伝播）
    int es = node->deref_size ? node->deref_size : (node->type_size ? node->type_size : 8);
    node_t *scaled = new_node(ND_MUL, idx, new_node_num(es));
    node_t *addr = new_node(ND_ADD, node, scaled);
    node_t *deref = calloc(1, sizeof(node_t));
    deref->kind = ND_DEREF;
    deref->lhs = addr;
    deref->type_size = es;
    node = deref;
  }
  return node;
}

// primary = ident "(" args? ")" | "(" expr ")" | ident | num | string
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
      int arg_cap = 4;
      node->args = calloc(arg_cap, sizeof(node_t*));
      if (!consume(')')) {
        node->args[nargs++] = expr();
        while (consume(',')) {
          if (nargs >= arg_cap) {
            arg_cap *= 2;
            node->args = realloc(node->args, sizeof(node_t*) * arg_cap);
          }
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
      node_t *node = calloc(1, sizeof(node_t));
      node->kind = ND_ADDR;
      node->lhs = new_node_lvar(var->offset - var->size + var->elem_size);
      node->type_size = var->elem_size; // 配列の要素サイズを伝播
      return node;
    }
    // ポインタ変数: 変数自体は8バイトロード、デリファレンス時は elem_size
    node_t *n = new_node_lvar_typed(var->offset, var->is_array ? 8 : (var->size > var->elem_size ? 8 : var->elem_size));
    n->deref_size = var->elem_size;
    n->is_float = var->is_float;
    return n;
  }

  // 文字列リテラル
  if (token->kind == TK_STRING) {
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_STRING;
    // ラベルを生成
    char label[32];
    snprintf(label, sizeof(label), ".LC%d", string_label_count++);
    node->string_label = strdup(label);
    // 文字列テーブルに登録
    string_lit_t *lit = calloc(1, sizeof(string_lit_t));
    lit->label = node->string_label;
    lit->str = token->str;
    lit->len = token->len;
    lit->next = string_literals;
    string_literals = lit;
    token = token->next;
    node->type_size = 8; // ポインタとして8バイト
    node->deref_size = 1; // 文字列は char* なので1バイト
    node->is_float = 0; // 文字列はポインタなので整数
    return node;
  }

  if (token->kind == TK_NUM) {
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_NUM;
    node->val = token->val;
    node->fval = token->fval;
    node->is_float = token->is_float;
    
    if (node->is_float) {
      // 浮動小数点リテラルを登録
      float_lit_t *lit = calloc(1, sizeof(float_lit_t));
      lit->id = float_label_count++;
      lit->fval = node->fval;
      lit->is_float = node->is_float;
      lit->next = float_literals;
      float_literals = lit;
      node->fval_id = lit->id;
    }
    
    token = token->next;
    return node;
  }

  error_at(token->str, "数値を期待しています");
  return NULL;
}
