#include "parser.h"
#include "parser_node_utils.h"
#include "parser_semantic_ctx.h"
#include "parser_decl.h"
#include "parser_expr.h"
#include "parser_loop_ctx.h"
#include "parser_switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

node_t **code;
string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;
static int string_label_count = 0;
static int float_label_count = 0;

static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
#define node_type_size pnode_type_size
#define node_deref_size pnode_deref_size
#define new_node_binary pnode_new_binary
#define new_node_num pnode_new_num
#define new_node_lvar pnode_new_lvar
#define new_node_lvar_typed pnode_new_lvar_typed
#define new_node_assign pnode_new_assign
#define expect_lvalue pnode_expect_lvalue
#define expect_incdec_target pnode_expect_incdec_target
#define new_compound_assign pnode_new_compound_assign

static node_t *stmt(void);
static node_t *declaration(void);
static node_t *assign(void);
static node_t *conditional(void);
static node_t *logical_or(void);
static node_t *logical_and(void);
static node_t *bit_or(void);
static node_t *bit_xor(void);
static node_t *bit_and(void);
static node_t *equality(void);
static node_t *relational(void);
static node_t *shift(void);
static node_t *add(void);
static node_t *mul(void);
static node_t *unary(void);
static node_t *primary(void);
static node_t *funcdef(void);



static bool is_type_token(token_kind_t kind) { return pctx_is_type_token(kind); }
static bool is_tag_keyword(token_kind_t kind) { return pctx_is_tag_keyword(kind); }
static int scalar_type_size(token_kind_t kind) { return pctx_scalar_type_size(kind); }

static int sizeof_expr_node(node_t *node) {
  int sz = node_type_size(node);
  if (sz) return sz;
  if (node && node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (node && node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 8;
}

static bool parse_cast_type(token_t *tok, token_kind_t *type_kind, int *is_pointer, token_t **after_rparen) {
  if (!tok || tok->kind != TK_LPAREN) return false;
  token_t *t = tok->next;
  if (!t || !is_type_token(t->kind)) return false;
  *type_kind = t->kind;
  t = t->next;
  *is_pointer = 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
  }
  if (!t || t->kind != TK_RPAREN) return false;
  *after_rparen = t->next;
  return true;
}

static node_t *apply_cast(token_kind_t type_kind, int is_pointer, node_t *operand) {
  if (is_pointer || type_kind == TK_LONG) {
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_FLOAT) {
    operand->fp_kind = TK_FLOAT_KIND_FLOAT;
    return operand;
  }
  if (type_kind == TK_DOUBLE) {
    operand->fp_kind = TK_FLOAT_KIND_DOUBLE;
    return operand;
  }
  if (type_kind == TK_INT) {
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_SHORT) {
    return new_node_binary(ND_BITAND, operand, new_node_num(0xffff));
  }
  if (type_kind == TK_CHAR) {
    return new_node_binary(ND_BITAND, operand, new_node_num(0xff));
  }
  tk_error_tok(token, "この型へのキャストは未対応です");
  return operand;
}

// program = funcdef*
void program(void) {
  int cap = 8;
  code = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!tk_at_eof()) {
    node_t *fn = funcdef();
    if (!fn) continue; // 関数プロトタイプ宣言はASTへ載せない
    if (i >= cap - 1) { // NULL終端用
      cap *= 2;
      code = realloc(code, sizeof(node_t*) * cap);
    }
    code[i++] = fn;
  }
  code[i] = NULL;
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
token_kind_t parser_consume_type_kind(void) {
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID || token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT || token->kind == TK_DOUBLE) {
    token_kind_t kind = token->kind;
    token = token->next;
    return kind;
  }
  return TK_EOF; // 型なし
}

static bool consume_type(void) {
  return parser_consume_type_kind() != TK_EOF;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  token_kind_t ret_kind = parser_consume_type_kind(); // 戻り値の型（省略可）
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  if (ret_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (ret_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  pexpr_set_current_func_ret_type(ret_token_kind, ret_fp_kind);
  token_ident_t *tok = tk_consume_ident();
  if (!tok) {
    tk_error_tok(token, "関数定義が期待されます");
  }
  node_func_t *node = calloc(1, sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->funcname = tok->str;
  node->funcname_len = tok->len;

  // 関数ごとにローカル変数テーブルをリセット
  pdecl_reset_locals();
  pctx_reset_function_scope();
  ploop_reset();

  tk_expect('(');
  // 仮引数のパース
  int arg_cap = 4;
  node->args = calloc(arg_cap, sizeof(node_t*));
  int nargs = 0;
  if (!tk_consume(')')) {
    consume_type(); // 仮引数の型
    while (tk_consume('*')) {} // ポインタの * を読み飛ばす
    token_ident_t *param = tk_consume_ident();
    if (param) {
      lvar_t *var = pdecl_register_lvar(param->str, param->len);
      node->args[nargs++] = new_node_lvar(var->offset);
    }
    while (tk_consume(',')) {
      if (nargs >= arg_cap) {
        arg_cap *= 2;
        node->args = realloc(node->args, sizeof(node_t*) * arg_cap);
      }
      consume_type(); // 仮引数の型
      while (tk_consume('*')) {} // ポインタの * を読み飛ばす
      param = tk_consume_ident();
      if (param) {
        lvar_t *var = pdecl_register_lvar(param->str, param->len);
        node->args[nargs++] = new_node_lvar(var->offset);
      }
    }
    tk_expect(')');
  }
  node->nargs = nargs;

  // 関数プロトタイプ宣言（本体なし）
  if (tk_consume(';')) {
    return NULL;
  }

  // 関数本体 (ブロック)
  tk_expect('{');
  node_block_t *body = calloc(1, sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 8;
  body->body = calloc(body_cap, sizeof(node_t*));
  while (!tk_consume('}')) {
    if (i >= body_cap - 1) {
      body_cap *= 2;
      body->body = realloc(body->body, sizeof(node_t*) * body_cap);
    }
    body->body[i++] = stmt();
  }
  body->body[i] = NULL;
  node->base.rhs = (node_t *)body;
  pctx_validate_goto_refs();

  return (node_t *)node;
}

static node_t *declaration(void) {
  return pdecl_parse_declaration();
}

// stmt = "{" stmt* "}"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "while" "(" expr ")" stmt
//      | "do" stmt "while" "(" expr ")" ";"
//      | "for" "(" expr? ";" expr? ";" expr? ")" stmt
//      | "switch" "(" expr ")" stmt
//      | "case" num ":" stmt
//      | "default" ":" stmt
//      | "break" ";"
//      | "continue" ";"
//      | "return" expr ";"
//      | "int" ident ("=" expr)? ";"
//      | expr ";"
static node_t *stmt(void) {
  if (tk_consume('{')) {
    node_block_t *node = calloc(1, sizeof(node_block_t));
    node->base.kind = ND_BLOCK;
    int i = 0;
    int cap = 8;
    node->body = calloc(cap, sizeof(node_t*));
    while (!tk_consume('}')) {
      if (i >= cap - 1) {
        cap *= 2;
        node->body = realloc(node->body, sizeof(node_t*) * cap);
      }
      node->body[i++] = stmt();
    }
    node->body[i] = NULL;
    return (node_t *)node;
  }

  // 型付き変数宣言: type "*"* ident ("[" num "]")? ("=" expr)? ";"
  if (is_type_token(token->kind)) {
    return declaration();
  }

  // タグ宣言 / タグ型参照
  if (is_tag_keyword(token->kind)) {
    token_kind_t tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      tk_error_tok(token, "タグ名が必要です");
    }
    if (tk_consume('{')) {
      tk_error_tok(token, "struct/union/enum のメンバ宣言は未対応です");
    }
    if (tk_consume(';')) {
      pctx_define_tag_type(tag_kind, tag->str, tag->len);
      return new_node_num(0);
    }
    if (!pctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      tk_error_tok(token, "未定義のタグ型 '%.*s' です", tag->len, tag->str);
    }
    return pdecl_parse_declaration_after_type(8, TK_FLOAT_KIND_NONE);
  }

  if (token->kind == TK_RETURN) {
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_RETURN;
    if (tk_consume(';')) {
      if (pexpr_current_func_ret_token_kind() != TK_VOID) {
        tk_error_tok(token, "void 以外の関数では return に式が必要です");
      }
      node->lhs = NULL;
      node->fp_kind = pexpr_current_func_ret_fp_kind();
      return node;
    }
    node->lhs = expr();
    if (pexpr_current_func_ret_token_kind() == TK_VOID) {
      tk_error_tok(token, "void 関数では return に式を指定できません");
    }
    node->fp_kind = pexpr_current_func_ret_fp_kind(); // 関数宣言時の戻り値型を記録
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_IF) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_IF;
    node->base.lhs = expr();
    tk_expect(')');
    node->base.rhs = stmt();
    if (token->kind == TK_ELSE) {
      token = token->next;
      node->els = stmt();
    }
    return (node_t *)node;
  }

  if (token->kind == TK_WHILE) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_WHILE;
    node->base.lhs = expr();  // 条件式
    tk_expect(')');
    ploop_enter();
    node->base.rhs = stmt();  // ループ本体
    ploop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_DO) {
    token = token->next;
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_DO_WHILE;
    ploop_enter();
    node->base.rhs = stmt();  // ループ本体
    ploop_leave();
    if (token->kind != TK_WHILE) {
      tk_error_tok(token, "'while'が必要です");
    }
    token = token->next;
    tk_expect('(');
    node->base.lhs = expr();  // 条件式
    tk_expect(')');
    tk_expect(';');
    return (node_t *)node;
  }

  if (token->kind == TK_FOR) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_FOR;
    if (!tk_consume(';')) {
      if (is_type_token(token->kind)) {
        node->init = declaration(); // 宣言は終端 ';' を含めて読む
      } else {
        node->init = expr();  // 初期化式
        tk_expect(';');
      }
    }
    if (!tk_consume(';')) {
      node->base.lhs = expr();   // 条件式
      tk_expect(';');
    }
    if (!tk_consume(')')) {
      node->inc = expr();   // インクリメント式
      tk_expect(')');
    }
    ploop_enter();
    node->base.rhs = stmt();     // ループ本体
    ploop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_SWITCH) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_SWITCH;
    node->base.lhs = expr();   // switch式
    tk_expect(')');
    psw_push_ctx();
    node->base.rhs = stmt();   // switch本体
    psw_pop_ctx();
    return (node_t *)node;
  }

  if (token->kind == TK_CASE) {
    token = token->next;
    node_case_t *node = calloc(1, sizeof(node_case_t));
    node->base.kind = ND_CASE;
    node->val = tk_expect_number();
    psw_register_case(node->val, token);
    tk_expect(':');
    node->base.rhs = stmt();
    return (node_t *)node;
  }

  if (token->kind == TK_DEFAULT) {
    token = token->next;
    psw_register_default(token);
    node_default_t *node = calloc(1, sizeof(node_default_t));
    node->base.kind = ND_DEFAULT;
    tk_expect(':');
    node->base.rhs = stmt();
    return (node_t *)node;
  }

  if (token->kind == TK_BREAK) {
    if (ploop_depth() == 0 && !psw_has_ctx()) {
      tk_error_tok(token, "break はループまたはswitch内でのみ使用できます");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_BREAK;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_CONTINUE) {
    if (ploop_depth() == 0) {
      tk_error_tok(token, "continue はループ内でのみ使用できます");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_CONTINUE;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_GOTO) {
    token_t *goto_tok = token;
    token = token->next;
    token_ident_t *ident = tk_consume_ident();
    if (!ident) {
      tk_error_tok(token, "goto の後にラベル名が必要です");
    }
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_GOTO;
    node->name = ident->str;
    node->name_len = ident->len;
    pctx_register_goto_ref(ident->str, ident->len, goto_tok);
    tk_expect(';');
    return (node_t *)node;
  }

  if (token->kind == TK_IDENT && token->next && token->next->kind == TK_COLON) {
    token_ident_t *ident = tk_consume_ident();
    tk_expect(':');
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_LABEL;
    node->name = ident->str;
    node->name_len = ident->len;
    pctx_register_label_def(ident->str, ident->len, token);
    node->base.rhs = stmt();
    return (node_t *)node;
  }

  node_t *node = expr();
  tk_expect(';');
  return node;
}

// expr = assign ("," assign)*
node_t *expr(void) {
  node_t *node = assign();
  while (tk_consume(',')) {
    node_t *rhs = assign();
    node_t *comma = new_node_binary(ND_COMMA, node, rhs);
    comma->fp_kind = rhs ? rhs->fp_kind : TK_FLOAT_KIND_NONE;
    node = comma;
  }
  return node;
}

// assign = conditional (("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "^=" | "|=") assign)?
static node_t *assign(void) {
  node_t *node = conditional();
  if (tk_consume('=')) {
    node_mem_t *assign_node = new_node_assign(node, assign());
    // 左辺のtype_sizeをASSIGNに伝播（str/strb/strh の切り替えに使用）
    assign_node->type_size = node_type_size(assign_node->base.lhs);
    assign_node->base.fp_kind = assign_node->base.lhs ? assign_node->base.lhs->fp_kind : 0;
    node = (node_t *)assign_node;
  } else if (tk_consume_str("+=")) {
    node = new_compound_assign(node, ND_ADD, assign(), "+=");
  } else if (tk_consume_str("-=")) {
    node = new_compound_assign(node, ND_SUB, assign(), "-=");
  } else if (tk_consume_str("*=")) {
    node = new_compound_assign(node, ND_MUL, assign(), "*=");
  } else if (tk_consume_str("/=")) {
    node = new_compound_assign(node, ND_DIV, assign(), "/=");
  } else if (tk_consume_str("%=")) {
    node = new_compound_assign(node, ND_MOD, assign(), "%=");
  } else if (tk_consume_str("<<=")) {
    node = new_compound_assign(node, ND_SHL, assign(), "<<=");
  } else if (tk_consume_str(">>=")) {
    node = new_compound_assign(node, ND_SHR, assign(), ">>=");
  } else if (tk_consume_str("&=")) {
    node = new_compound_assign(node, ND_BITAND, assign(), "&=");
  } else if (tk_consume_str("^=")) {
    node = new_compound_assign(node, ND_BITXOR, assign(), "^=");
  } else if (tk_consume_str("|=")) {
    node = new_compound_assign(node, ND_BITOR, assign(), "|=");
  }
  return node;
}

node_t *parser_assign_expr(void) { return assign(); }

// conditional = logical_or ("?" expr ":" conditional)?
static node_t *conditional(void) {
  node_t *node = logical_or();
  if (tk_consume('?')) {
    node_ctrl_t *ternary = calloc(1, sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.lhs = node;
    ternary->base.rhs = expr();
    tk_expect(':');
    ternary->els = conditional(); // 右結合
    // 三項演算の結果型は then/else の優先型を採用
    ternary->base.fp_kind = ternary->base.rhs->fp_kind;
    if (ternary->els && ternary->els->fp_kind > ternary->base.fp_kind) {
      ternary->base.fp_kind = ternary->els->fp_kind;
    }
    return (node_t *)ternary;
  }
  return node;
}

// logical_or = logical_and ("||" logical_and)*
static node_t *logical_or(void) {
  node_t *node = logical_and();
  while (tk_consume_str("||")) {
    node = new_node_binary(ND_LOGOR, node, logical_and());
  }
  return node;
}

// logical_and = bit_or ("&&" bit_or)*
static node_t *logical_and(void) {
  node_t *node = bit_or();
  while (tk_consume_str("&&")) {
    node = new_node_binary(ND_LOGAND, node, bit_or());
  }
  return node;
}

// bit_or = bit_xor ("|" bit_xor)*
static node_t *bit_or(void) {
  node_t *node = bit_xor();
  while (tk_consume('|')) {
    node = new_node_binary(ND_BITOR, node, bit_xor());
  }
  return node;
}

// bit_xor = bit_and ("^" bit_and)*
static node_t *bit_xor(void) {
  node_t *node = bit_and();
  while (tk_consume('^')) {
    node = new_node_binary(ND_BITXOR, node, bit_and());
  }
  return node;
}

// bit_and = equality ("&" equality)*
static node_t *bit_and(void) {
  node_t *node = equality();
  while (tk_consume('&')) {
    node = new_node_binary(ND_BITAND, node, equality());
  }
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
static node_t *equality(void) {
  node_t *node = relational();

  for (;;) {
    if (tk_consume_str("=="))
      node = new_node_binary(ND_EQ, node, relational());
    else if (tk_consume_str("!="))
      node = new_node_binary(ND_NE, node, relational());
    else
      return node;
  }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static node_t *relational(void) {
  node_t *node = shift();

  for (;;) {
    if (tk_consume_str("<"))
      node = new_node_binary(ND_LT, node, shift());
    else if (tk_consume_str("<="))
      node = new_node_binary(ND_LE, node, shift());
    else if (tk_consume_str(">"))
      node = new_node_binary(ND_LT, shift(), node);
    else if (tk_consume_str(">="))
      node = new_node_binary(ND_LE, shift(), node);
    else
      return node;
  }
}

// shift = add ("<<" add | ">>" add)*
static node_t *shift(void) {
  node_t *node = add();
  for (;;) {
    if (tk_consume_str("<<"))
      node = new_node_binary(ND_SHL, node, add());
    else if (tk_consume_str(">>"))
      node = new_node_binary(ND_SHR, node, add());
    else
      return node;
  }
}

// add = mul ("+" mul | "-" mul)*
static node_t *add(void) {
  node_t *node = mul();

  for (;;) {
    if (tk_consume('+'))
      node = new_node_binary(ND_ADD, node, mul());
    else if (tk_consume('-'))
      node = new_node_binary(ND_SUB, node, mul());
    else
      return node;
  }
}

// mul = unary ("*" unary | "/" unary | "%" unary)*
static node_t *mul(void) {
  node_t *node = unary();

  for (;;) {
    if (tk_consume('*'))
      node = new_node_binary(ND_MUL, node, unary());
    else if (tk_consume('/'))
      node = new_node_binary(ND_DIV, node, unary());
    else if (tk_consume('%'))
      node = new_node_binary(ND_MOD, node, unary());
    else
      return node;
  }
}

// unary = ("++" | "--" | "+" | "-" | "!" | "~" | "*" | "&") unary | primary postfix*
// postfix = "[" expr "]"
static node_t *unary(void) {
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  if (parse_cast_type(token, &cast_kind, &cast_is_ptr, &after_rparen)) {
    token = after_rparen;
    if (cast_kind == TK_VOID && !cast_is_ptr) {
      tk_error_tok(token, "void へのキャストは未対応です");
    }
    return apply_cast(cast_kind, cast_is_ptr, unary());
  }

  if (token->kind == TK_SIZEOF) {
    token = token->next;
    if (tk_consume('(')) {
      if (is_type_token(token->kind)) {
        token_kind_t type_kind = parser_consume_type_kind();
        if (type_kind == TK_VOID) {
          tk_error_tok(token, "sizeof(void) はサポートしていません");
        }
        int sz = scalar_type_size(type_kind);
        while (tk_consume('*')) {
          sz = 8;
        }
        tk_expect(')');
        return new_node_num(sz);
      }
      node_t *node = expr();
      tk_expect(')');
      return new_node_num(sizeof_expr_node(node));
    }
    return new_node_num(sizeof_expr_node(unary()));
  }

  if (tk_consume_str("++")) {
    node_t *target = unary();
    expect_incdec_target(target, "++");
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_PRE_INC;
    node->lhs = target;
    return node;
  }
  if (tk_consume_str("--")) {
    node_t *target = unary();
    expect_incdec_target(target, "--");
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_PRE_DEC;
    node->lhs = target;
    return node;
  }
  if (tk_consume('+')) {
    return unary();
  }
  if (tk_consume('-')) {
    return new_node_binary(ND_SUB, new_node_num(0), unary());
  }
  if (tk_consume('!')) {
    return new_node_binary(ND_EQ, unary(), new_node_num(0));
  }
  if (tk_consume('~')) {
    // ~x == -x - 1 (2の補数表現)
    node_t *neg = new_node_binary(ND_SUB, new_node_num(0), unary());
    return new_node_binary(ND_SUB, neg, new_node_num(1));
  }
  if (tk_consume('*')) {
    node_t *operand = unary();
    node_mem_t *node = calloc(1, sizeof(node_mem_t));
    node->base.kind = ND_DEREF;
    node->base.lhs = operand;
    node->base.fp_kind = operand ? operand->fp_kind : 0;
    // デリファレンス結果のサイズ: オペランドが指す先の要素サイズ
    int ds = node_deref_size(operand);
    node->type_size = ds ? ds : 8;
    return (node_t *)node;
  }
  if (tk_consume('&')) {
    node_mem_t *node = calloc(1, sizeof(node_mem_t));
    node->base.kind = ND_ADDR;
    node->base.lhs = unary();
    return (node_t *)node;
  }
  node_t *node = primary();
  // 後置演算子: [expr]
  while (tk_consume('[')) {
    // arr[i] → *(arr + i*elem_size)
    node_t *idx = expr();
    tk_expect(']');
    // 要素サイズを取得（nodeから伝播、デフォルトは8）
    // 要素サイズを取得（deref_size > type_size の優先度で伝播）
    int ds = node_deref_size(node);
    int ts = node_type_size(node);
    int es = ds ? ds : (ts ? ts : 8);
    node_t *scaled = new_node_binary(ND_MUL, idx, new_node_num(es));
    node_t *addr = new_node_binary(ND_ADD, node, scaled);
    node_mem_t *deref = calloc(1, sizeof(node_mem_t));
    deref->base.kind = ND_DEREF;
    deref->base.lhs = addr;
    deref->type_size = es;
    node = (node_t *)deref;
  }
  for (;;) {
    if (tk_consume_str("++")) {
      expect_incdec_target(node, "++");
      node_t *inc = calloc(1, sizeof(node_t));
      inc->kind = ND_POST_INC;
      inc->lhs = node;
      node = inc;
      continue;
    }
    if (tk_consume_str("--")) {
      expect_incdec_target(node, "--");
      node_t *dec = calloc(1, sizeof(node_t));
      dec->kind = ND_POST_DEC;
      dec->lhs = node;
      node = dec;
      continue;
    }
    break;
  }
  return node;
}

// primary = ident "(" args? ")" | "(" expr ")" | ident | num | string
// args    = assign ("," assign)*
static node_t *primary(void) {
  if (tk_consume('(')) {
    node_t *node = expr();
    tk_expect(')');
    return node;
  }

  token_ident_t *tok = tk_consume_ident();
  if (tok) {
    // 関数呼び出し: ident "(" args? ")"
    if (tk_consume('(')) {
      node_func_t *node = calloc(1, sizeof(node_func_t));
      node->base.kind = ND_FUNCALL;
      node->funcname = tok->str;
      node->funcname_len = tok->len;
      int nargs = 0;
      int arg_cap = 4;
      node->args = calloc(arg_cap, sizeof(node_t*));
      if (!tk_consume(')')) {
        node->args[nargs++] = assign();
        while (tk_consume(',')) {
          if (nargs >= arg_cap) {
            arg_cap *= 2;
            node->args = realloc(node->args, sizeof(node_t*) * arg_cap);
          }
          node->args[nargs++] = assign();
        }
        tk_expect(')');
      }
      node->nargs = nargs;
      return (node_t *)node;
    }
    // ローカル変数
    lvar_t *var = pdecl_find_lvar(tok->str, tok->len);
    if (!var) {
      var = pdecl_register_lvar(tok->str, tok->len);
    }
    if (var->is_array) {
      // 配列名は先頭要素のアドレスを返す
      node_mem_t *node = calloc(1, sizeof(node_mem_t));
      node->base.kind = ND_ADDR;
      node->base.lhs = new_node_lvar(var->offset - var->size + var->elem_size);
      node->type_size = var->elem_size; // 配列の要素サイズを伝播
      node->deref_size = var->elem_size;
      return (node_t *)node;
    }
    // ポインタ変数: 変数自体は8バイトロード、デリファレンス時は elem_size
    node_t *n = new_node_lvar_typed(var->offset, var->is_array ? 8 : (var->size > var->elem_size ? 8 : var->elem_size));
    as_lvar(n)->mem.deref_size = var->elem_size;
    n->fp_kind = var->fp_kind;
    return n;
  }

  // 文字列リテラル
  if (token->kind == TK_STRING) {
    tk_char_width_t merged_width = TK_CHAR_WIDTH_CHAR;
    tk_string_prefix_kind_t merged_prefix_kind = TK_STR_PREFIX_NONE;
    int total_len = 0;
    token_t *t = token;
    while (t && t->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)t;
      if (total_len == 0) {
        merged_width = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
        merged_prefix_kind = st->str_prefix_kind;
      } else if (merged_width != st->char_width) {
        tk_error_tok(t, "異なる接頭辞の文字列リテラルは連結できません");
      }
      total_len += st->len;
      t = t->next;
    }

    char *merged = calloc((size_t)total_len + 1, 1);
    int off = 0;
    while (token && token->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)token;
      memcpy(merged + off, st->str, (size_t)st->len);
      off += st->len;
      token = token->next;
    }

    node_string_t *node = calloc(1, sizeof(node_string_t));
    node->mem.base.kind = ND_STRING;
    // ラベルを生成
    char label[32];
    snprintf(label, sizeof(label), ".LC%d", string_label_count++);
    node->string_label = strdup(label);
    // 文字列テーブルに登録
    string_lit_t *lit = calloc(1, sizeof(string_lit_t));
    lit->label = node->string_label;
    lit->str = merged;
    lit->len = total_len;
    lit->char_width = merged_width;
    lit->str_prefix_kind = merged_prefix_kind;
    lit->next = string_literals;
    string_literals = lit;
    node->mem.type_size = 8; // ポインタとして8バイト
    node->mem.deref_size = merged_width;
    node->mem.base.fp_kind = TK_FLOAT_KIND_NONE; // 文字列はポインタなので整数
    node->char_width = merged_width;
    node->str_prefix_kind = merged_prefix_kind;
    return (node_t *)node;
  }

  if (token->kind == TK_NUM) {
    token_num_t *num = (token_num_t *)token;
    node_num_t *node = calloc(1, sizeof(node_num_t));
    node->base.kind = ND_NUM;
    if (num->num_kind == TK_NUM_KIND_INT) {
      node->base.fp_kind = TK_FLOAT_KIND_NONE;
      node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      node->val = tk_as_num_int(token)->val;
    } else {
      node->base.fp_kind = tk_as_num_float(token)->fp_kind;
      node->float_suffix_kind = tk_as_num_float(token)->float_suffix_kind;
      node->fval = tk_as_num_float(token)->fval;
    }

    if (node->base.fp_kind) {
      // 浮動小数点リテラルを登録
      float_lit_t *lit = calloc(1, sizeof(float_lit_t));
      lit->id = float_label_count++;
      lit->fval = node->fval;
      lit->fp_kind = node->base.fp_kind;
      lit->float_suffix_kind = node->float_suffix_kind;
      lit->next = float_literals;
      float_literals = lit;
      node->fval_id = lit->id;
    }

    token = token->next;
    return (node_t *)node;
  }

  tk_error_tok(token, "数値を期待しています");
  return NULL;
}
