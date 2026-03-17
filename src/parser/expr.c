#include "internal/expr.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static token_kind_t g_current_ret_token_kind = TK_INT;
static tk_float_kind_t g_current_ret_fp_kind = TK_FLOAT_KIND_NONE;
static int string_label_count = 0;
static int float_label_count = 0;

static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

static int sizeof_expr_node(node_t *node) {
  int sz = psx_node_type_size(node);
  if (sz) return sz;
  if (node && node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (node && node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 8;
}

static int parse_cast_type(token_t *tok, token_kind_t *type_kind, int *is_pointer, token_t **after_rparen) {
  if (!tok || tok->kind != TK_LPAREN) return 0;
  token_t *t = tok->next;
  bool is_type = false;
  psx_ctx_get_type_info(t ? t->kind : TK_EOF, &is_type, NULL);
  if (!t || !is_type) return 0;
  *type_kind = t->kind;
  t = t->next;
  *is_pointer = 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
  }
  if (!t || t->kind != TK_RPAREN) return 0;
  *after_rparen = t->next;
  return 1;
}

static node_t *expr_internal(void);
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

void psx_expr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind) {
  g_current_ret_token_kind = ret_kind;
  g_current_ret_fp_kind = fp_kind;
}

token_kind_t psx_expr_current_func_ret_token_kind(void) {
  return g_current_ret_token_kind;
}

tk_float_kind_t psx_expr_current_func_ret_fp_kind(void) {
  return g_current_ret_fp_kind;
}

// expr = assign ("," assign)*
node_t *psx_expr_expr(void) {
  return expr_internal();
}

// assign = conditional (("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "^=" | "|=") assign)?
node_t *psx_expr_assign(void) {
  return assign();
}

static node_t *expr_internal(void) {
  node_t *node = assign();
  while (token->kind == TK_COMMA) {
    token = token->next;
    node_t *rhs = assign();
    node_t *comma = psx_node_new_binary(ND_COMMA, node, rhs);
    comma->fp_kind = rhs ? rhs->fp_kind : TK_FLOAT_KIND_NONE;
    node = comma;
  }
  return node;
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
  if (type_kind == TK_VOID) {
    // 現状ASTでは専用ノードを持たず、既存ノードのまま評価値を捨てる文脈で利用する。
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_SHORT) {
    return psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(0xffff));
  }
  if (type_kind == TK_CHAR) {
    return psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(0xff));
  }
  psx_diag_ctx(token, "cast", "この型へのキャストは未対応です");
  return operand;
}

static node_t *assign(void) {
  node_t *node = conditional();
  switch (token->kind) {
    case TK_ASSIGN: {
      token = token->next;
      node_mem_t *assign_node = psx_node_new_assign(node, assign());
      assign_node->type_size = psx_node_type_size(assign_node->base.lhs);
      assign_node->base.fp_kind = assign_node->base.lhs ? assign_node->base.lhs->fp_kind : 0;
      node = (node_t *)assign_node;
      break;
    }
    case TK_PLUSEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_ADD, assign(), "+="); break;
    case TK_MINUSEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_SUB, assign(), "-="); break;
    case TK_MULEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_MUL, assign(), "*="); break;
    case TK_DIVEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_DIV, assign(), "/="); break;
    case TK_MODEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_MOD, assign(), "%="); break;
    case TK_SHLEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_SHL, assign(), "<<="); break;
    case TK_SHREQ: token = token->next; node = psx_node_new_compound_assign(node, ND_SHR, assign(), ">>="); break;
    case TK_ANDEQ: token = token->next; node = psx_node_new_compound_assign(node, ND_BITAND, assign(), "&="); break;
    case TK_XOREQ: token = token->next; node = psx_node_new_compound_assign(node, ND_BITXOR, assign(), "^="); break;
    case TK_OREQ: token = token->next; node = psx_node_new_compound_assign(node, ND_BITOR, assign(), "|="); break;
    default: break;
  }
  return node;
}

static node_t *conditional(void) {
  node_t *node = logical_or();
  if (token->kind == TK_QUESTION) {
    token = token->next;
    node_ctrl_t *ternary = calloc(1, sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.lhs = node;
    ternary->base.rhs = expr_internal();
    tk_expect(':');
    ternary->els = conditional();
    ternary->base.fp_kind = ternary->base.rhs->fp_kind;
    if (ternary->els && ternary->els->fp_kind > ternary->base.fp_kind) {
      ternary->base.fp_kind = ternary->els->fp_kind;
    }
    return (node_t *)ternary;
  }
  return node;
}

static node_t *logical_or(void) {
  node_t *node = logical_and();
  while (token->kind == TK_OROR) {
    token = token->next;
    node = psx_node_new_binary(ND_LOGOR, node, logical_and());
  }
  return node;
}

static node_t *logical_and(void) {
  node_t *node = bit_or();
  while (token->kind == TK_ANDAND) {
    token = token->next;
    node = psx_node_new_binary(ND_LOGAND, node, bit_or());
  }
  return node;
}

static node_t *bit_or(void) {
  node_t *node = bit_xor();
  while (token->kind == TK_PIPE) {
    token = token->next;
    node = psx_node_new_binary(ND_BITOR, node, bit_xor());
  }
  return node;
}

static node_t *bit_xor(void) {
  node_t *node = bit_and();
  while (token->kind == TK_CARET) {
    token = token->next;
    node = psx_node_new_binary(ND_BITXOR, node, bit_and());
  }
  return node;
}

static node_t *bit_and(void) {
  node_t *node = equality();
  while (token->kind == TK_AMP) {
    token = token->next;
    node = psx_node_new_binary(ND_BITAND, node, equality());
  }
  return node;
}

static node_t *equality(void) {
  node_t *node = relational();
  for (;;) {
    if (token->kind == TK_EQEQ) {
      token = token->next;
      node = psx_node_new_binary(ND_EQ, node, relational());
    } else if (token->kind == TK_NEQ) {
      token = token->next;
      node = psx_node_new_binary(ND_NE, node, relational());
    }
    else return node;
  }
}

static node_t *relational(void) {
  node_t *node = shift();
  for (;;) {
    if (token->kind == TK_LT) {
      token = token->next;
      node = psx_node_new_binary(ND_LT, node, shift());
    } else if (token->kind == TK_LE) {
      token = token->next;
      node = psx_node_new_binary(ND_LE, node, shift());
    } else if (token->kind == TK_GT) {
      token = token->next;
      node = psx_node_new_binary(ND_LT, shift(), node);
    } else if (token->kind == TK_GE) {
      token = token->next;
      node = psx_node_new_binary(ND_LE, shift(), node);
    }
    else return node;
  }
}

static node_t *shift(void) {
  node_t *node = add();
  for (;;) {
    if (token->kind == TK_SHL) {
      token = token->next;
      node = psx_node_new_binary(ND_SHL, node, add());
    } else if (token->kind == TK_SHR) {
      token = token->next;
      node = psx_node_new_binary(ND_SHR, node, add());
    }
    else return node;
  }
}

static node_t *add(void) {
  node_t *node = mul();
  for (;;) {
    if (token->kind == TK_PLUS) {
      token = token->next;
      node = psx_node_new_binary(ND_ADD, node, mul());
    } else if (token->kind == TK_MINUS) {
      token = token->next;
      node = psx_node_new_binary(ND_SUB, node, mul());
    }
    else return node;
  }
}

static node_t *mul(void) {
  node_t *node = unary();
  for (;;) {
    if (token->kind == TK_MUL) {
      token = token->next;
      node = psx_node_new_binary(ND_MUL, node, unary());
    } else if (token->kind == TK_DIV) {
      token = token->next;
      node = psx_node_new_binary(ND_DIV, node, unary());
    } else if (token->kind == TK_MOD) {
      token = token->next;
      node = psx_node_new_binary(ND_MOD, node, unary());
    }
    else return node;
  }
}

static node_t *unary(void) {
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  if (parse_cast_type(token, &cast_kind, &cast_is_ptr, &after_rparen)) {
    token = after_rparen;
    return apply_cast(cast_kind, cast_is_ptr, unary());
  }

  if (token->kind == TK_SIZEOF) {
    token = token->next;
    if (token->kind == TK_LPAREN) {
      token = token->next;
      bool is_type = false;
      int scalar_size = 8;
      token_kind_t type_kind = token->kind;
      psx_ctx_get_type_info(type_kind, &is_type, &scalar_size);
      if (is_type) {
        token = token->next;
        if (type_kind == TK_VOID) {
          psx_diag_ctx(token, "sizeof", "sizeof(void) はサポートしていません");
        }
        int sz = scalar_size;
        while (token->kind == TK_MUL) {
          token = token->next;
          sz = 8;
        }
        tk_expect(')');
        return psx_node_new_num(sz);
      }
      node_t *node = expr_internal();
      tk_expect(')');
      return psx_node_new_num(sizeof_expr_node(node));
    }
    return psx_node_new_num(sizeof_expr_node(unary()));
  }

  if (token->kind == TK_INC) {
    token = token->next;
    node_t *target = unary();
    psx_node_expect_incdec_target(target, "++");
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_PRE_INC;
    node->lhs = target;
    return node;
  }
  if (token->kind == TK_DEC) {
    token = token->next;
    node_t *target = unary();
    psx_node_expect_incdec_target(target, "--");
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_PRE_DEC;
    node->lhs = target;
    return node;
  }
  if (token->kind == TK_PLUS) {
    token = token->next;
    return unary();
  }
  if (token->kind == TK_MINUS) {
    token = token->next;
    return psx_node_new_binary(ND_SUB, psx_node_new_num(0), unary());
  }
  if (token->kind == TK_BANG) {
    token = token->next;
    return psx_node_new_binary(ND_EQ, unary(), psx_node_new_num(0));
  }
  if (token->kind == TK_TILDE) {
    token = token->next;
    node_t *neg = psx_node_new_binary(ND_SUB, psx_node_new_num(0), unary());
    return psx_node_new_binary(ND_SUB, neg, psx_node_new_num(1));
  }
  if (token->kind == TK_MUL) {
    token = token->next;
    node_t *operand = unary();
    node_mem_t *node = calloc(1, sizeof(node_mem_t));
    node->base.kind = ND_DEREF;
    node->base.lhs = operand;
    node->base.fp_kind = operand ? operand->fp_kind : 0;
    int ds = psx_node_deref_size(operand);
    node->type_size = ds ? ds : 8;
    return (node_t *)node;
  }
  if (token->kind == TK_AMP) {
    token = token->next;
    node_mem_t *node = calloc(1, sizeof(node_mem_t));
    node->base.kind = ND_ADDR;
    node->base.lhs = unary();
    return (node_t *)node;
  }

  node_t *node = primary();
  while (token->kind == TK_LBRACKET) {
    token = token->next;
    node_t *idx = expr_internal();
    tk_expect(']');
    int ds = psx_node_deref_size(node);
    int ts = psx_node_type_size(node);
    int es = ds ? ds : (ts ? ts : 8);
    node_t *scaled = psx_node_new_binary(ND_MUL, idx, psx_node_new_num(es));
    node_t *addr = psx_node_new_binary(ND_ADD, node, scaled);
    node_mem_t *deref = calloc(1, sizeof(node_mem_t));
    deref->base.kind = ND_DEREF;
    deref->base.lhs = addr;
    deref->type_size = es;
    node = (node_t *)deref;
  }
  for (;;) {
    if (token->kind == TK_INC) {
      token = token->next;
      psx_node_expect_incdec_target(node, "++");
      node_t *inc = calloc(1, sizeof(node_t));
      inc->kind = ND_POST_INC;
      inc->lhs = node;
      node = inc;
      continue;
    }
    if (token->kind == TK_DEC) {
      token = token->next;
      psx_node_expect_incdec_target(node, "--");
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

static node_t *primary(void) {
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

  if (token->kind == TK_LPAREN) {
    token = token->next;
    node_t *node = expr_internal();
    tk_expect(')');
    return node;
  }

  token_ident_t *tok = tk_consume_ident();
  if (tok) {
    if (token->kind == TK_LPAREN) {
      token = token->next;
      node_func_t *node = calloc(1, sizeof(node_func_t));
      node->base.kind = ND_FUNCALL;
      node->funcname = tok->str;
      node->funcname_len = tok->len;
      int nargs = 0;
      int arg_cap = 16;
      node->args = calloc(arg_cap, sizeof(node_t*));
      if (token->kind == TK_RPAREN) {
        token = token->next;
      } else {
        node->args[nargs++] = assign();
        while (token->kind == TK_COMMA) {
          token = token->next;
          if (nargs >= arg_cap) {
            arg_cap = pda_next_cap(arg_cap, nargs + 1);
            node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
          }
          node->args[nargs++] = assign();
        }
        tk_expect(')');
      }
      node->nargs = nargs;
      return (node_t *)node;
    }

    lvar_t *var = psx_decl_find_lvar(tok->str, tok->len);
    if (!var) {
      var = psx_decl_register_lvar(tok->str, tok->len);
    }
    if (var->is_array) {
      node_mem_t *node = calloc(1, sizeof(node_mem_t));
      node->base.kind = ND_ADDR;
      node->base.lhs = psx_node_new_lvar(var->offset - var->size + var->elem_size);
      node->type_size = var->elem_size;
      node->deref_size = var->elem_size;
      return (node_t *)node;
    }
    node_t *n = psx_node_new_lvar_typed(var->offset, var->is_array ? 8 : (var->size > var->elem_size ? 8 : var->elem_size));
    as_lvar(n)->mem.deref_size = var->elem_size;
    n->fp_kind = var->fp_kind;
    return n;
  }

  if (token->kind == TK_STRING) {
    tk_char_width_t merged_width = TK_CHAR_WIDTH_CHAR;
    tk_string_prefix_kind_t merged_prefix_kind = TK_STR_PREFIX_NONE;
    size_t total_len = 0;
    token_t *t = token;
    while (t && t->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)t;
      if (total_len == 0) {
        merged_width = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
        merged_prefix_kind = st->str_prefix_kind;
      } else if (merged_width != st->char_width) {
        tk_error_tok(t, "異なる接頭辞の文字列リテラルは連結できません");
      }
      if (st->len < 0 || (size_t)st->len > SIZE_MAX - total_len - 1) {
        tk_error_tok(t, "文字列リテラルが大きすぎます");
      }
      total_len += (size_t)st->len;
      t = t->next;
    }

    if (total_len > (size_t)INT_MAX) {
      tk_error_tok(token, "文字列リテラルが大きすぎます");
    }
    char *merged = calloc(total_len + 1, 1);
    if (!merged) {
      tk_error_tok(token, "メモリ確保に失敗しました");
    }
    size_t off = 0;
    while (token && token->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)token;
      if (st->len < 0 || (size_t)st->len > total_len - off) {
        tk_error_tok(token, "文字列連結中にサイズが不正です");
      }
      memcpy(merged + off, st->str, (size_t)st->len);
      off += (size_t)st->len;
      token = token->next;
    }

    node_string_t *node = calloc(1, sizeof(node_string_t));
    node->mem.base.kind = ND_STRING;
    int id = string_label_count++;
    int label_len = snprintf(NULL, 0, ".LC%d", id);
    node->string_label = calloc((size_t)label_len + 1, 1);
    snprintf(node->string_label, (size_t)label_len + 1, ".LC%d", id);
    string_lit_t *lit = calloc(1, sizeof(string_lit_t));
    lit->label = node->string_label;
    lit->str = merged;
    lit->len = (int)total_len;
    lit->char_width = merged_width;
    lit->str_prefix_kind = merged_prefix_kind;
    lit->next = string_literals;
    string_literals = lit;
    node->mem.type_size = 8;
    node->mem.deref_size = merged_width;
    node->mem.base.fp_kind = TK_FLOAT_KIND_NONE;
    node->char_width = merged_width;
    node->str_prefix_kind = merged_prefix_kind;
    return (node_t *)node;
  }

  psx_diag_ctx(token, "primary", "数値を期待しています");
  return NULL;
}
