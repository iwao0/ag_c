#include "internal/expr.h"
#include "internal/arena.h"
#include "internal/core.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static token_kind_t g_current_ret_token_kind = TK_INT;
static tk_float_kind_t g_current_ret_fp_kind = TK_FLOAT_KIND_NONE;
static int g_current_ret_struct_size = 0;
static char *g_current_funcname = NULL;
static int g_current_funcname_len = 0;
static int string_label_count = 0;
static int float_label_count = 0;
static int compound_lit_seq = 0;
static int g_expr_nest_depth = 0;
static int g_paren_nest_depth = 0;

#define PS_MAX_EXPR_NEST_DEPTH 1024
#define PS_MAX_PAREN_NEST_DEPTH 1024

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

typedef struct {
  token_kind_t kind;
  int is_pointer;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int ptr_levels;
  int ptr_deref_size;
  int ptr_base_deref_size;
  tk_float_kind_t ptr_pointee_fp_kind;
  int ptr_pointee_const;
  int ptr_pointee_volatile;
} generic_type_t;

static void consume_local_type_quals(token_t **cur);
static long long eval_const_expr_type_size(node_t *n, int *ok);
static void apply_array_abstract_suffix_size(int *sz);
static int is_type_name_start_token(token_t *t);

static void enter_expr_nest_or_die(void) {
  g_expr_nest_depth++;
  if (g_expr_nest_depth > PS_MAX_EXPR_NEST_DEPTH) {
    psx_diag_ctx(curtok(), "expr", "式ネストが深すぎます（上限 %d）", PS_MAX_EXPR_NEST_DEPTH);
  }
}

static void leave_expr_nest(void) {
  if (g_expr_nest_depth > 0) g_expr_nest_depth--;
}

static void enter_paren_nest_or_die(void) {
  g_paren_nest_depth++;
  if (g_paren_nest_depth > PS_MAX_PAREN_NEST_DEPTH) {
    psx_diag_ctx(curtok(), "paren", "括弧ネストが深すぎます（上限 %d）", PS_MAX_PAREN_NEST_DEPTH);
  }
}

static void leave_paren_nest(void) {
  if (g_paren_nest_depth > 0) g_paren_nest_depth--;
}

static int sizeof_expr_node(node_t *node) {
  int sz = psx_node_type_size(node);
  if (sz) return sz;
  if (node && node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (node && node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 8;
}

static long long eval_const_expr_type_size(node_t *n, int *ok) {
  if (!n) {
    *ok = 0;
    return 0;
  }
  switch (n->kind) {
    case ND_NUM:
      return ((node_num_t *)n)->val;
    case ND_ADD: {
      long long l = eval_const_expr_type_size(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_type_size(n->rhs, ok);
      if (!*ok) return 0;
      return l + r;
    }
    case ND_SUB: {
      long long l = eval_const_expr_type_size(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_type_size(n->rhs, ok);
      if (!*ok) return 0;
      return l - r;
    }
    case ND_MUL: {
      long long l = eval_const_expr_type_size(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_type_size(n->rhs, ok);
      if (!*ok) return 0;
      return l * r;
    }
    case ND_DIV: {
      long long l = eval_const_expr_type_size(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_type_size(n->rhs, ok);
      if (!*ok || r == 0) {
        *ok = 0;
        return 0;
      }
      return l / r;
    }
    case ND_COMMA:
      (void)eval_const_expr_type_size(n->lhs, ok);
      if (!*ok) return 0;
      return eval_const_expr_type_size(n->rhs, ok);
    default:
      *ok = 0;
      return 0;
  }
}

static void apply_array_abstract_suffix_size(int *sz) {
  while (tk_consume('[')) {
    if (tk_consume(']')) {
      psx_diag_ctx(curtok(), "sizeof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED));
    }
    node_t *dim_expr = psx_expr_assign();
    int ok = 1;
    long long dim = eval_const_expr_type_size(dim_expr, &ok);
    if (!ok) {
      psx_diag_ctx(curtok(), "sizeof", diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                   diag_text_for(DIAG_TEXT_ARRAY_SIZE));
    }
    if (dim <= 0) {
      psx_diag_ctx(curtok(), "sizeof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    tk_expect(']');
    *sz *= (int)dim;
  }
}

static token_t *skip_balanced_paren_token(token_t *start) {
  if (!start || start->kind != TK_LPAREN) return NULL;
  int depth = 0;
  for (token_t *t = start; t; t = t->next) {
    if (t->kind == TK_LPAREN) depth++;
    else if (t->kind == TK_RPAREN) {
      depth--;
      if (depth == 0) return t->next;
    }
    if (t->kind == TK_EOF) break;
  }
  return NULL;
}

static token_t *skip_balanced_bracket_token(token_t *start) {
  if (!start || start->kind != TK_LBRACKET) return NULL;
  int depth = 0;
  for (token_t *t = start; t; t = t->next) {
    if (t->kind == TK_LBRACKET) depth++;
    else if (t->kind == TK_RBRACKET) {
      depth--;
      if (depth == 0) return t->next;
    }
    if (t->kind == TK_EOF) break;
  }
  return NULL;
}

static int parse_funcptr_abstract_decl(token_t **ptok, int *is_pointer) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
    consume_local_type_quals(&t);
  }
  if (t && t->kind == TK_IDENT) t = t->next; // named declaratorも許可
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  *ptok = after_params;
  *is_pointer = 1;
  return 1;
}

static int parse_array_of_funcptr_abstract_decl(token_t **ptok, int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LBRACKET) return 0;

  int array_mul = 1;
  while (t && t->kind == TK_LBRACKET) {
    token_t *open = t;
    token_t *after = skip_balanced_bracket_token(open);
    if (!after) return 0;
    token_t *dim_tok = open->next;
    if (!dim_tok || dim_tok->kind != TK_NUM || tk_as_num(dim_tok)->num_kind != TK_NUM_KIND_INT ||
        !dim_tok->next || dim_tok->next->kind != TK_RBRACKET) {
      return 0;
    }
    int dim = (int)tk_as_num_int(dim_tok)->uval;
    if (dim <= 0) return 0;
    array_mul *= dim;
    t = after;
  }

  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  *ptok = after_params;
  if (out_array_mul) *out_array_mul = array_mul;
  return 1;
}

static int parse_ptr_to_array_abstract_decl(token_t **ptok, int *is_pointer) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
    consume_local_type_quals(&t);
  }
  if (t && t->kind == TK_IDENT) t = t->next;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  token_t *after = t;
  while (after && after->kind == TK_LBRACKET) {
    after = skip_balanced_bracket_token(after);
    if (!after) return 0;
  }
  *ptok = after;
  *is_pointer = 1;
  return 1;
}

static int parse_array_of_ptr_to_array_abstract_decl(token_t **ptok, int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LBRACKET) return 0;

  int array_mul = 1;
  while (t && t->kind == TK_LBRACKET) {
    token_t *open = t;
    token_t *after = skip_balanced_bracket_token(open);
    if (!after) return 0;
    token_t *dim_tok = open->next;
    if (!dim_tok || dim_tok->kind != TK_NUM || tk_as_num(dim_tok)->num_kind != TK_NUM_KIND_INT ||
        !dim_tok->next || dim_tok->next->kind != TK_RBRACKET) {
      return 0;
    }
    int dim = (int)tk_as_num_int(dim_tok)->uval;
    if (dim <= 0) return 0;
    array_mul *= dim;
    t = after;
  }

  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  token_t *after = t;
  while (after && after->kind == TK_LBRACKET) {
    after = skip_balanced_bracket_token(after);
    if (!after) return 0;
  }
  *ptok = after;
  if (out_array_mul) *out_array_mul = array_mul;
  return 1;
}

// Parse nested abstract declarator like: int (*(*[N])[M])
// and return the outer array multiplier N so sizeof(type) can be computed.
static int parse_array_of_ptr_to_array_of_ptr_abstract_decl(token_t **ptok, int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LBRACKET) return 0;
  if (!t->next || t->next->kind != TK_NUM || tk_as_num(t->next)->num_kind != TK_NUM_KIND_INT) return 0;
  int n = (int)tk_as_num_int(t->next)->uval;
  if (n <= 0) return 0;
  t = t->next->next;
  if (!t || t->kind != TK_RBRACKET) return 0;
  t = t->next;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  token_t *after_inner_array = skip_balanced_bracket_token(t);
  if (!after_inner_array) return 0;
  t = after_inner_array;
  if (!t || t->kind != TK_RPAREN) return 0;
  *ptok = t->next;
  if (out_array_mul) *out_array_mul = n;
  return 1;
}

// Parse abstract declarator like: int (*(*)(void))[3]
static int parse_ptr_to_func_returning_ptr_to_array_abstract_decl(token_t **ptok) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  while (t && t->kind == TK_LBRACKET) {
    t = skip_balanced_bracket_token(t);
    if (!t) return 0;
  }
  *ptok = t;
  return 1;
}

// Parse abstract declarator like: int (*(*[N])(void))[M]
static int parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(token_t **ptok,
                                                                            int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LBRACKET) return 0;
  if (!t->next || t->next->kind != TK_NUM || tk_as_num(t->next)->num_kind != TK_NUM_KIND_INT) return 0;
  int n = (int)tk_as_num_int(t->next)->uval;
  if (n <= 0) return 0;
  t = t->next->next;
  if (!t || t->kind != TK_RBRACKET) return 0;
  t = t->next;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  while (t && t->kind == TK_LBRACKET) {
    t = skip_balanced_bracket_token(t);
    if (!t) return 0;
  }
  *ptok = t;
  if (out_array_mul) *out_array_mul = n;
  return 1;
}

// Parse abstract declarator like: int (*(*)(void))(int)
static int parse_ptr_to_func_returning_ptr_to_func_abstract_decl(token_t **ptok) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  *ptok = after_params;
  return 1;
}

// Parse abstract declarator like: int (*(*(*)(void))(int))[3]
static int parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(token_t **ptok) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after = skip_balanced_paren_token(t);
  if (!after) return 0;
  t = after;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  after = skip_balanced_paren_token(t);
  if (!after) return 0;
  t = after;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  while (t && t->kind == TK_LBRACKET) {
    t = skip_balanced_bracket_token(t);
    if (!t) return 0;
  }
  *ptok = t;
  return 1;
}

static int is_type_name_start_token(token_t *t) {
  if (!t) return 0;
  if (t->kind == TK_CONST || t->kind == TK_VOLATILE || t->kind == TK_RESTRICT || t->kind == TK_ATOMIC) return 1;
  if (t->kind == TK_STRUCT || t->kind == TK_UNION || t->kind == TK_ENUM) return 1;
  if (psx_ctx_is_type_token(t->kind)) return 1;
  if (psx_ctx_is_typedef_name_token(t)) return 1;
  return 0;
}

static void consume_local_type_quals(token_t **cur) {
  while (*cur && ((*cur)->kind == TK_CONST || (*cur)->kind == TK_VOLATILE || (*cur)->kind == TK_RESTRICT)) {
    *cur = (*cur)->next;
  }
}

static void consume_cast_pointer_suffix(token_t **cur, int *is_pointer) {
  consume_local_type_quals(cur);
  while (*cur && (*cur)->kind == TK_MUL) {
    *is_pointer = 1;
    *cur = (*cur)->next;
    consume_local_type_quals(cur);
  }
}

static int parse_integer_cast_spec_sequence(token_t *start, token_kind_t *out_kind, int *out_elem_size,
                                            token_t **out_next) {
  if (!start) return 0;
  if (start->kind != TK_SIGNED && start->kind != TK_UNSIGNED &&
      start->kind != TK_SHORT && start->kind != TK_LONG &&
      start->kind != TK_INT && start->kind != TK_CHAR) {
    return 0;
  }

  int n_signed = 0, n_unsigned = 0, n_short = 0, n_long = 0, n_int = 0, n_char = 0;
  token_t *t = start;
  while (t && (t->kind == TK_SIGNED || t->kind == TK_UNSIGNED ||
               t->kind == TK_SHORT || t->kind == TK_LONG ||
               t->kind == TK_INT || t->kind == TK_CHAR)) {
    if (t->kind == TK_SIGNED) n_signed++;
    else if (t->kind == TK_UNSIGNED) n_unsigned++;
    else if (t->kind == TK_SHORT) n_short++;
    else if (t->kind == TK_LONG) n_long++;
    else if (t->kind == TK_INT) n_int++;
    else if (t->kind == TK_CHAR) n_char++;
    t = t->next;
  }

  if (n_signed > 1 || n_unsigned > 1 || n_short > 1 || n_long > 2 || n_int > 1 || n_char > 1) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }
  if (n_signed && n_unsigned) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }
  if (n_short && n_long) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }
  if (n_char && (n_short || n_long || n_int)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }

  token_kind_t kind = TK_INT;
  int elem = 4;
  if (n_char) {
    kind = TK_CHAR;
    elem = 1;
  } else if (n_short) {
    kind = TK_SHORT;
    elem = 2;
  } else if (n_long) {
    kind = TK_LONG;
    elem = 8;
  } else if (n_unsigned) {
    kind = TK_UNSIGNED;
    elem = 4;
  } else if (n_signed) {
    kind = TK_SIGNED;
    elem = 4;
  }

  if (out_kind) *out_kind = kind;
  if (out_elem_size) *out_elem_size = elem;
  if (out_next) *out_next = t;
  return 1;
}

static generic_type_t infer_generic_control_type(node_t *control) {
  generic_type_t gt = {TK_INT, 0, TK_EOF, NULL, 0, 0, 0, 0, TK_FLOAT_KIND_NONE, 0, 0};
  if (!control) return gt;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(control, &gt.tag_kind, &gt.tag_name, &gt.tag_len, &is_tag_ptr);
  if (!is_tag_ptr && (gt.tag_kind == TK_STRUCT || gt.tag_kind == TK_UNION)) {
    gt.kind = gt.tag_kind;
    return gt;
  }
  if (control->kind == ND_STRING) {
    gt.kind = TK_CHAR;
    gt.is_pointer = 1;
    return gt;
  }
  if (control->fp_kind == TK_FLOAT_KIND_FLOAT) {
    gt.kind = TK_FLOAT;
    return gt;
  }
  if (control->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    gt.kind = TK_DOUBLE;
    return gt;
  }
  int ts = psx_node_type_size(control);
  int ds = psx_node_deref_size(control);
  if (ts == 8 && ds > 0) {
    gt.is_pointer = 1;
    gt.kind = TK_INT;
    gt.ptr_levels = psx_node_pointer_qual_levels(control);
    gt.ptr_deref_size = ds;
    gt.ptr_base_deref_size = psx_node_base_deref_size(control);
    gt.ptr_pointee_fp_kind = psx_node_pointee_fp_kind(control);
    if (control->kind == ND_LVAR) {
      gt.ptr_pointee_const = ((node_lvar_t *)control)->mem.is_const_qualified;
      gt.ptr_pointee_volatile = ((node_lvar_t *)control)->mem.is_volatile_qualified;
    } else if (control->kind == ND_GVAR || control->kind == ND_DEREF || control->kind == ND_ASSIGN ||
               control->kind == ND_ADDR || control->kind == ND_STRING) {
      gt.ptr_pointee_const = ((node_mem_t *)control)->is_const_qualified;
      gt.ptr_pointee_volatile = ((node_mem_t *)control)->is_volatile_qualified;
    }
    return gt;
  }
  if (ts == 1) gt.kind = TK_CHAR;
  else if (ts == 2) gt.kind = TK_SHORT;
  else if (ts == 8) gt.kind = TK_LONG;
  else gt.kind = TK_INT;
  return gt;
}

static int generic_type_matches(generic_type_t control, generic_type_t assoc) {
  if (control.is_pointer != assoc.is_pointer) return 0;
  if (control.is_pointer) {
    if (control.ptr_levels && assoc.ptr_levels && control.ptr_levels != assoc.ptr_levels) return 0;
    // struct/union ポインタはタグ一致で比較
    if (control.tag_kind != TK_EOF || assoc.tag_kind != TK_EOF) {
      return control.tag_kind == assoc.tag_kind &&
             control.tag_len == assoc.tag_len &&
             strncmp(control.tag_name ? control.tag_name : "",
                     assoc.tag_name ? assoc.tag_name : "",
                     (size_t)control.tag_len) == 0;
    }
    // 浮動小数点ポインタは pointee FP 種別で比較
    if (control.ptr_pointee_fp_kind != TK_FLOAT_KIND_NONE ||
        assoc.ptr_pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      return control.ptr_pointee_fp_kind == assoc.ptr_pointee_fp_kind &&
             control.ptr_pointee_const == assoc.ptr_pointee_const &&
             control.ptr_pointee_volatile == assoc.ptr_pointee_volatile;
    }
    // それ以外は pointee サイズで比較（int*/char*/short*/long* など）
    if (control.ptr_levels >= 2 || assoc.ptr_levels >= 2) {
      return control.ptr_base_deref_size == assoc.ptr_base_deref_size &&
             control.ptr_pointee_const == assoc.ptr_pointee_const &&
             control.ptr_pointee_volatile == assoc.ptr_pointee_volatile;
    }
    return control.ptr_deref_size == assoc.ptr_deref_size &&
           control.ptr_pointee_const == assoc.ptr_pointee_const &&
           control.ptr_pointee_volatile == assoc.ptr_pointee_volatile;
  }
  if (control.kind == TK_STRUCT || control.kind == TK_UNION) {
    return control.kind == assoc.kind &&
           control.tag_len == assoc.tag_len &&
           strncmp(control.tag_name ? control.tag_name : "",
                   assoc.tag_name ? assoc.tag_name : "",
                   (size_t)control.tag_len) == 0;
  }
  return control.kind == assoc.kind;
}

static int parse_generic_assoc_type(generic_type_t *out) {
  out->kind = TK_EOF;
  out->is_pointer = 0;
  out->tag_kind = TK_EOF;
  out->tag_name = NULL;
  out->tag_len = 0;
  out->ptr_levels = 0;
  out->ptr_deref_size = 0;
  out->ptr_base_deref_size = 0;
  out->ptr_pointee_fp_kind = TK_FLOAT_KIND_NONE;
  out->ptr_pointee_const = 0;
  out->ptr_pointee_volatile = 0;
  int base_elem_size = 8;
  tk_float_kind_t base_fp_kind = TK_FLOAT_KIND_NONE;
  int base_const = 0;
  int base_volatile = 0;
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
    if (curtok()->kind == TK_CONST) base_const = 1;
    if (curtok()->kind == TK_VOLATILE) base_volatile = 1;
    set_curtok(curtok()->next);
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    token_ident_t *id = (token_ident_t *)curtok();
    token_kind_t base_kind = TK_EOF;
    int elem_size = 8;
    tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
    token_kind_t tag_kind = TK_EOF;
    char *tag_name = NULL;
    int tag_len = 0;
    int is_ptr = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len,
                              &is_ptr, &base_const, &base_volatile);
    set_curtok(curtok()->next);
    out->kind = (tag_kind != TK_EOF) ? tag_kind : base_kind;
    out->is_pointer = is_ptr;
    out->tag_kind = tag_kind;
    out->tag_name = tag_name;
    out->tag_len = tag_len;
    base_elem_size = elem_size;
    base_fp_kind = fp_kind;
  } else if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    token_kind_t tag_kind = curtok()->kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) return 0;
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name((token_t *)tag, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
    }
    out->kind = tag_kind;
    out->tag_kind = tag_kind;
    out->tag_name = tag->str;
    out->tag_len = tag->len;
    base_elem_size = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
  } else {
    token_kind_t tk = psx_consume_type_kind();
    if (tk == TK_EOF) return 0;
    out->kind = tk;
    psx_ctx_get_type_info(tk, NULL, &base_elem_size);
    if (tk == TK_FLOAT) base_fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (tk == TK_DOUBLE) base_fp_kind = TK_FLOAT_KIND_DOUBLE;
  }
  token_t *t = curtok();
  while (t && t->kind == TK_MUL) {
    out->is_pointer = 1;
    out->ptr_levels++;
    t = t->next;
    consume_local_type_quals(&t);
  }
  // type-name の abstract-declarator の一部を受理:
  //  - int (*)(int)
  //  - int (*)[3]
  //  - int (*[3])(int)
  // これにより _Generic の関連型でも cast/sizeof と同系統の型名を扱える。
  (void)parse_funcptr_abstract_decl(&t, &out->is_pointer);
  (void)parse_ptr_to_array_abstract_decl(&t, &out->is_pointer);
  (void)parse_array_of_funcptr_abstract_decl(&t, NULL);
  (void)parse_array_of_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  (void)parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t);
  (void)parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  if (!out->is_pointer) {
    while (t && t->kind == TK_LBRACKET) {
      token_t *after = skip_balanced_bracket_token(t);
      if (!after) break;
      t = after;
    }
  }
  if (out->is_pointer) {
    if (out->ptr_levels == 0) out->ptr_levels = 1;
    out->ptr_deref_size = base_elem_size;
    out->ptr_base_deref_size = base_elem_size;
    out->ptr_pointee_fp_kind = base_fp_kind;
    out->ptr_pointee_const = base_const;
    out->ptr_pointee_volatile = base_volatile;
  }
  set_curtok(t);
  return 1;
}

static node_t *build_member_access(node_t *base, int from_ptr, token_t *op_tok) {
  token_ident_t *member = tk_consume_ident();
  if (!member) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
  }

  token_kind_t base_tag_kind = TK_EOF;
  char *base_tag_name = NULL;
  int base_tag_len = 0;
  int base_is_ptr = 0;
  psx_node_get_tag_type(base, &base_tag_kind, &base_tag_name, &base_tag_len, &base_is_ptr);
  if (base_tag_kind == TK_EOF || (!from_ptr && base_is_ptr) || (from_ptr && !base_is_ptr)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, op_tok,
                   "%s",
                   diag_message_for(from_ptr
                                        ? DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR
                                        : DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT));
  }

  int off = 0, mem_size = 0, mem_deref = 0;
  int mem_array_len = 0;
  token_kind_t mem_tag_kind = TK_EOF;
  char *mem_tag_name = NULL;
  int mem_tag_len = 0;
  int mem_is_ptr = 0;
  if (!psx_ctx_find_tag_member(base_tag_kind, base_tag_name, base_tag_len,
                               member->str, member->len,
                               &off, &mem_size, &mem_deref, &mem_array_len,
                               &mem_tag_kind, &mem_tag_name, &mem_tag_len, &mem_is_ptr)) {
    psx_diag_ctx(op_tok, "member", diag_message_for(DIAG_ERR_PARSER_MEMBER_NOT_FOUND),
                 member->len, member->str);
  }

  // ビットフィールドメタデータを取得
  int bf_width = 0, bf_offset = 0, bf_is_signed = 0;
  psx_ctx_get_tag_member_bf(base_tag_kind, base_tag_name, base_tag_len,
                            member->str, member->len,
                            &bf_width, &bf_offset, &bf_is_signed);

  node_t *addr_base = base;
  if (!from_ptr) {
    node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
    addr->base.kind = ND_ADDR;
    addr->base.lhs = base;
    addr->type_size = 8;
    addr_base = (node_t *)addr;
  }
  node_t *addr = psx_node_new_binary(ND_ADD, addr_base, psx_node_new_num(off));
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  deref->type_size = mem_size ? mem_size : 8;
  deref->deref_size = mem_deref;
  if (mem_array_len > 0 && !mem_is_ptr && mem_size > 0) {
    // Keep array shape for postfix [] (e.g. s.a[1]).
    deref->type_size = mem_size * mem_array_len;
    deref->deref_size = mem_size;
  }
  deref->tag_kind = mem_tag_kind;
  deref->tag_name = mem_tag_name;
  deref->tag_len = mem_tag_len;
  deref->is_tag_pointer = mem_is_ptr;
  deref->bit_width = bf_width;
  deref->bit_offset = bf_offset;
  deref->bit_is_signed = bf_is_signed;
  return (node_t *)deref;
}

static int parse_cast_type(token_t *tok, token_kind_t *type_kind, int *is_pointer, token_t **after_rparen,
                           token_kind_t *out_tag_kind, char **out_tag_name, int *out_tag_len,
                           int *out_elem_size, tk_float_kind_t *out_fp_kind, int *out_array_count) {
  if (!tok || tok->kind != TK_LPAREN) return 0;
  token_t *t = tok->next;
  if (!t) return 0;
  *type_kind = TK_EOF;
  if (out_tag_kind) *out_tag_kind = TK_EOF;
  if (out_tag_name) *out_tag_name = NULL;
  if (out_tag_len) *out_tag_len = 0;
  if (out_elem_size) *out_elem_size = 8;
  if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_NONE;
  if (out_array_count) *out_array_count = 0;

  consume_local_type_quals(&t);
  if (t && (t->kind == TK_THREAD_LOCAL || t->kind == TK_EXTERN || t->kind == TK_STATIC ||
            t->kind == TK_AUTO || t->kind == TK_REGISTER || t->kind == TK_TYPEDEF)) {
    psx_diag_ctx(t, "cast", "%s",
                 diag_message_for(DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN));
  }
  if (t && t->kind == TK_ATOMIC && !(t->next && t->next->kind == TK_LPAREN)) {
    t = t->next;
    consume_local_type_quals(&t);
  }

  if (t->kind == TK_ATOMIC && t->next && t->next->kind == TK_LPAREN) {
    token_t *q = t->next->next;
    token_kind_t inner_kind = TK_EOF;
    token_kind_t inner_tag_kind = TK_EOF;
    char *inner_tag_name = NULL;
    int inner_tag_len = 0;
    int inner_ptr = 0;
    int inner_elem = 8;
    tk_float_kind_t inner_fp = TK_FLOAT_KIND_NONE;
    int nested_atomic_wrappers = 0;
    while (q && q->kind == TK_ATOMIC && q->next && q->next->kind == TK_LPAREN) {
      nested_atomic_wrappers++;
      q = q->next->next;
      consume_local_type_quals(&q);
    }
    if (q && q->kind == TK_ATOMIC && !(q->next && q->next->kind == TK_LPAREN)) q = q->next;
    consume_local_type_quals(&q);

    bool inner_is_type = false;
    bool q_is_type = false;
    if (q) psx_ctx_get_type_info(q->kind, &q_is_type, NULL);
    if (q_is_type) {
      inner_is_type = true;
      if (q->kind == TK_LONG && q->next && q->next->kind == TK_DOUBLE) {
        inner_kind = TK_DOUBLE;
        inner_elem = 8;
        inner_fp = TK_FLOAT_KIND_DOUBLE;
        q = q->next->next;
      } else if (parse_integer_cast_spec_sequence(q, &inner_kind, &inner_elem, &q)) {
        inner_fp = TK_FLOAT_KIND_NONE;
      } else {
        inner_kind = q->kind;
        psx_ctx_get_type_info(inner_kind, NULL, &inner_elem);
        if (inner_kind == TK_FLOAT) inner_fp = TK_FLOAT_KIND_FLOAT;
        else if (inner_kind == TK_DOUBLE) inner_fp = TK_FLOAT_KIND_DOUBLE;
        q = q->next;
      }
    } else if (q && psx_ctx_is_tag_keyword(q->kind)) {
      token_kind_t tag_kind = q->kind;
      q = q->next;
      token_ident_t *tag = (token_ident_t *)q;
      if (!q || q->kind != TK_IDENT) return 0;
      if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
        psx_diag_undefined_with_name(q, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
      }
      inner_kind = tag_kind;
      inner_tag_kind = tag_kind;
      inner_tag_name = tag->str;
      inner_tag_len = tag->len;
      inner_elem = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
      inner_is_type = true;
      q = q->next;
    } else if (q && psx_ctx_is_typedef_name_token(q)) {
      token_ident_t *id = (token_ident_t *)q;
      token_kind_t td_base = TK_EOF;
      int td_elem = 8;
      tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
      token_kind_t td_tag = TK_EOF;
      char *td_tag_name = NULL;
      int td_tag_len = 0;
      int td_ptr = 0;
      psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len,
                                &td_ptr, NULL, NULL);
      inner_kind = (td_tag != TK_EOF) ? td_tag : td_base;
      inner_tag_kind = td_tag;
      inner_tag_name = td_tag_name;
      inner_tag_len = td_tag_len;
      inner_elem = td_elem;
      inner_fp = td_fp;
      inner_ptr = td_ptr;
      inner_is_type = true;
      q = q->next;
    }
    if (!inner_is_type) return 0;
    consume_cast_pointer_suffix(&q, &inner_ptr);
    while (nested_atomic_wrappers-- > 0) {
      if (!q || q->kind != TK_RPAREN) return 0;
      q = q->next;
    }
    if (!q || q->kind != TK_RPAREN) return 0;
    *type_kind = inner_kind;
    *is_pointer = inner_ptr;
    if (out_tag_kind) *out_tag_kind = inner_tag_kind;
    if (out_tag_name) *out_tag_name = inner_tag_name;
    if (out_tag_len) *out_tag_len = inner_tag_len;
    if (out_elem_size) *out_elem_size = inner_elem;
    if (out_fp_kind) *out_fp_kind = inner_fp;
    t = q->next;
    goto cast_parse_postfix;
  }

  bool is_type = false;
  psx_ctx_get_type_info(t->kind, &is_type, NULL);
  // Minimal support for C11 complex/imaginary cast spellings:
  //   (_Complex float), (_Imaginary double), (double _Complex), ...
  if (t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY) {
    token_t *q = t->next;
    if (q && q->kind == TK_LONG && q->next && q->next->kind == TK_DOUBLE) {
      *type_kind = TK_DOUBLE;
      if (out_elem_size) *out_elem_size = 8;
      if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      t = q->next->next;
      is_type = true;
    } else if (q && (q->kind == TK_FLOAT || q->kind == TK_DOUBLE)) {
      *type_kind = q->kind;
      if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
      if (out_fp_kind) {
        if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
        else *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      }
      t = q->next;
      is_type = true;
    } else {
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, t,
                     "%s",
                     diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
    }
  } else if ((t->kind == TK_FLOAT || t->kind == TK_DOUBLE || t->kind == TK_LONG) &&
             t->next && (t->next->kind == TK_COMPLEX || t->next->kind == TK_IMAGINARY)) {
    if (t->kind == TK_LONG) {
      if (!t->next || t->next->kind != TK_DOUBLE || !t->next->next ||
          (t->next->next->kind != TK_COMPLEX && t->next->next->kind != TK_IMAGINARY)) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, t,
                       "%s",
                       diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
      }
      *type_kind = TK_DOUBLE;
      if (out_elem_size) *out_elem_size = 8;
      if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      t = t->next->next->next;
    } else {
      *type_kind = t->kind;
      if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
      if (out_fp_kind) {
        if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
        else *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      }
      t = t->next->next;
    }
    is_type = true;
  }
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
    *type_kind = TK_DOUBLE;
    if (out_elem_size) *out_elem_size = 8;
    if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
    t = t->next->next;
    is_type = true;
  }
  if (is_type) {
    if (*type_kind == TK_EOF) {
      if (parse_integer_cast_spec_sequence(t, type_kind, out_elem_size, &t)) {
        if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_NONE;
      } else {
        *type_kind = t->kind;
        if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
        if (out_fp_kind) {
          if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
          else if (*type_kind == TK_DOUBLE) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
        }
        t = t->next;
      }
    }
  } else if (psx_ctx_is_tag_keyword(t->kind)) {
    token_kind_t tag_kind = t->kind;
    t = t->next;
    token_ident_t *tag = (token_ident_t *)t;
    if (!t || t->kind != TK_IDENT) return 0;
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name(t, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
    }
    *type_kind = tag_kind;
    if (out_tag_kind) *out_tag_kind = tag_kind;
    if (out_tag_name) *out_tag_name = tag->str;
    if (out_tag_len) *out_tag_len = tag->len;
    if (out_elem_size) *out_elem_size = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    token_ident_t *id = (token_ident_t *)t;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_ptr = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len,
                              &td_ptr, NULL, NULL);
    *type_kind = (td_tag != TK_EOF) ? td_tag : td_base;
    if (out_tag_kind) *out_tag_kind = td_tag;
    if (out_tag_name) *out_tag_name = td_tag_name;
    if (out_tag_len) *out_tag_len = td_tag_len;
    if (out_elem_size) *out_elem_size = td_elem;
    if (out_fp_kind) *out_fp_kind = td_fp;
    *is_pointer = td_ptr;
    t = t->next;
  } else {
    return 0;
  }

cast_parse_postfix:
  if (*is_pointer != 1) *is_pointer = 0;
  consume_cast_pointer_suffix(&t, is_pointer);
  parse_funcptr_abstract_decl(&t, is_pointer);
  (void)parse_ptr_to_array_abstract_decl(&t, is_pointer);
  (void)parse_array_of_funcptr_abstract_decl(&t, NULL);
  (void)parse_array_of_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  (void)parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t);
  (void)parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  // 配列宣言子 [N] を受理する（非ポインタ型のみ）
  if (!*is_pointer && t && t->kind == TK_LBRACKET) {
    t = t->next;
    int n = 0;
    if (t && t->kind == TK_NUM && tk_as_num(t)->num_kind == TK_NUM_KIND_INT) {
      n = (int)tk_as_num_int(t)->uval;
      t = t->next;
    }
    if (!t || t->kind != TK_RBRACKET) return 0;
    t = t->next;
    if (out_array_count) *out_array_count = n;
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
static node_t *cast(void);
static node_t *unary(void);
static node_t *primary(void);
static node_t *apply_postfix(node_t *node);

static int is_same_tag_nonscalar_expr(node_t *expr, token_kind_t cast_kind, char *cast_tag_name, int cast_tag_len) {
  if (!expr) return 0;
  node_t *v = expr;
  while (v && v->kind == ND_COMMA) v = v->rhs;
  if (!v) return 0;
  if (v->kind == ND_TERNARY) {
    node_ctrl_t *t = (node_ctrl_t *)v;
    return is_same_tag_nonscalar_expr(t->base.rhs, cast_kind, cast_tag_name, cast_tag_len) &&
           is_same_tag_nonscalar_expr(t->els, cast_kind, cast_tag_name, cast_tag_len);
  }
  token_kind_t op_tag_kind = TK_EOF;
  char *op_tag_name = NULL;
  int op_tag_len = 0;
  int op_is_tag_ptr = 0;
  psx_node_get_tag_type(v, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
  return !op_is_tag_ptr && op_tag_kind == cast_kind && op_tag_len == cast_tag_len &&
         strncmp(op_tag_name ? op_tag_name : "", cast_tag_name ? cast_tag_name : "", (size_t)cast_tag_len) == 0;
}

static int is_size_compatible_nonscalar_expr(node_t *expr, token_kind_t cast_kind, int cast_elem_size) {
  if (!expr) return 0;
  node_t *v = expr;
  while (v && v->kind == ND_COMMA) v = v->rhs;
  if (!v) return 0;
  if (v->kind == ND_TERNARY) {
    node_ctrl_t *t = (node_ctrl_t *)v;
    return is_size_compatible_nonscalar_expr(t->base.rhs, cast_kind, cast_elem_size) &&
           is_size_compatible_nonscalar_expr(t->els, cast_kind, cast_elem_size);
  }
  token_kind_t op_tag_kind = TK_EOF;
  char *op_tag_name = NULL;
  int op_tag_len = 0;
  int op_is_tag_ptr = 0;
  psx_node_get_tag_type(v, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
  if (op_is_tag_ptr || op_tag_kind != cast_kind) return 0;
  int op_sz = psx_node_type_size(v);
  return op_sz > 0 && cast_elem_size > 0 && op_sz == cast_elem_size;
}

static char *new_compound_lit_name(void) {
  int n = compound_lit_seq++;
  int len = snprintf(NULL, 0, "__compound_lit_%d", n);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__compound_lit_%d", n);
  return name;
}

static node_t *new_typed_lvar_ref(lvar_t *var, int is_pointer) {
  node_t *ref = psx_node_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
  ref->fp_kind = var->fp_kind;
  as_lvar(ref)->mem.deref_size = var->elem_size;
  as_lvar(ref)->mem.is_pointer = is_pointer;
  as_lvar(ref)->mem.tag_kind = var->tag_kind;
  as_lvar(ref)->mem.tag_name = var->tag_name;
  as_lvar(ref)->mem.tag_len = var->tag_len;
  as_lvar(ref)->mem.is_tag_pointer = var->is_tag_pointer;
  as_lvar(ref)->mem.is_const_qualified = var->is_const_qualified;
  as_lvar(ref)->mem.is_volatile_qualified = var->is_volatile_qualified;
  as_lvar(ref)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  as_lvar(ref)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  as_lvar(ref)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  as_lvar(ref)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  as_lvar(ref)->mem.pointer_qual_levels = var->pointer_qual_levels;
  as_lvar(ref)->mem.base_deref_size = var->base_deref_size;
  as_lvar(ref)->mem.is_unsigned = var->is_unsigned;
  return ref;
}

static node_t *new_member_lvar_ref(lvar_t *owner, int member_offset, int member_type_size,
                                   token_kind_t member_tag_kind, char *member_tag_name,
                                   int member_tag_len, int member_is_tag_pointer) {
  node_t *lvar = psx_node_new_lvar_typed(owner->offset + member_offset, member_type_size);
  as_lvar(lvar)->mem.tag_kind = member_tag_kind;
  as_lvar(lvar)->mem.tag_name = member_tag_name;
  as_lvar(lvar)->mem.tag_len = member_tag_len;
  as_lvar(lvar)->mem.is_tag_pointer = member_is_tag_pointer;
  return lvar;
}

static node_t *lower_union_value_cast(node_t *operand,
                                      token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                      int cast_elem_size, tk_float_kind_t cast_fp_kind) {
  int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
  char *tmp_name = new_compound_lit_name();
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), base_elem, base_elem, 0);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  var->is_tag_pointer = 0;
  var->fp_kind = cast_fp_kind;

  char *member_name = NULL;
  int member_len = 0;
  int member_offset = 0;
  int member_type_size = 0;
  int member_array_len = 0;
  token_kind_t member_tag_kind = TK_EOF;
  char *member_tag_name = NULL;
  int member_tag_len = 0;
  int member_is_tag_pointer = 0;
  int member_count = psx_ctx_get_tag_member_count(cast_tag_kind, cast_tag_name, cast_tag_len);
  bool found = false;
  for (int ordinal = 0; ordinal < member_count; ordinal++) {
    found = psx_ctx_get_tag_member_at(cast_tag_kind, cast_tag_name, cast_tag_len, ordinal,
                                      &member_name, &member_len,
                                      &member_offset, &member_type_size, NULL, &member_array_len,
                                      &member_tag_kind, &member_tag_name,
                                      &member_tag_len, &member_is_tag_pointer);
    if (!found) break;
    if (member_len > 0) break;
  }
  if (!found || member_len <= 0) {
    psx_diag_ctx(curtok(), "cast", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  node_t *lhs = new_member_lvar_ref(var, member_offset, member_type_size,
                                    member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, operand);
  assign_node->type_size = member_type_size;

  node_t *ref = new_typed_lvar_ref(var, 0);
  return psx_node_new_binary(ND_COMMA, (node_t *)assign_node, ref);
}

static node_t *lower_struct_value_cast(node_t *operand,
                                       token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                       int cast_elem_size, tk_float_kind_t cast_fp_kind) {
  int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
  char *tmp_name = new_compound_lit_name();
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), base_elem, base_elem, 0);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  var->is_tag_pointer = 0;
  var->fp_kind = cast_fp_kind;

  char *member_name = NULL;
  int member_len = 0;
  int member_offset = 0;
  int member_type_size = 0;
  int member_array_len = 0;
  token_kind_t member_tag_kind = TK_EOF;
  char *member_tag_name = NULL;
  int member_tag_len = 0;
  int member_is_tag_pointer = 0;
  int member_count = psx_ctx_get_tag_member_count(cast_tag_kind, cast_tag_name, cast_tag_len);
  bool found = false;
  for (int ordinal = 0; ordinal < member_count; ordinal++) {
    found = psx_ctx_get_tag_member_at(cast_tag_kind, cast_tag_name, cast_tag_len, ordinal,
                                      &member_name, &member_len,
                                      &member_offset, &member_type_size, NULL, &member_array_len,
                                      &member_tag_kind, &member_tag_name,
                                      &member_tag_len, &member_is_tag_pointer);
    if (!found) break;
    if (member_len > 0) break;
  }
  if (!found || member_len <= 0) {
    psx_diag_ctx(curtok(), "cast", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  node_t *lhs = new_member_lvar_ref(var, member_offset, member_type_size,
                                    member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, operand);
  assign_node->type_size = member_type_size;

  node_t *ref = new_typed_lvar_ref(var, 0);
  return psx_node_new_binary(ND_COMMA, (node_t *)assign_node, ref);
}

static int parse_parenthesized_type_size(void) {
  token_t *t = curtok();
  if (t->kind == TK_LPAREN && is_type_name_start_token(t->next)) {
    set_curtok(t->next);
    int sz = parse_parenthesized_type_size();
    if (sz < 0) return -1;
    tk_expect(')');
    return sz;
  }

  // Minimal support for C11 complex/imaginary spellings in sizeof/alignof:
  //   _Complex float, _Imaginary double, float _Complex, double _Imaginary
  if (t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY) {
    t = t->next;
    int sz = 0;
    if (t->kind == TK_FLOAT) {
      sz = 4 * 2; // _Complex float = 8B
      t = t->next;
    } else if (t->kind == TK_DOUBLE) {
      sz = 8 * 2; // _Complex double = 16B
      t = t->next;
    } else if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
      sz = 8 * 2; // _Complex long double = 16B (lowering)
      t = t->next->next;
    } else {
      return -1;
    }
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }
  if ((t->kind == TK_FLOAT || t->kind == TK_DOUBLE) &&
      t->next && (t->next->kind == TK_COMPLEX || t->next->kind == TK_IMAGINARY)) {
    int base_sz = (t->kind == TK_FLOAT) ? 4 : 8;
    int sz = base_sz * 2; // _Complex: 基底型の2倍
    t = t->next->next;
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE &&
      t->next->next &&
      (t->next->next->kind == TK_COMPLEX || t->next->next->kind == TK_IMAGINARY)) {
    int sz = 8 * 2; // _Complex long double = 16B (lowering)
    t = t->next->next->next;
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }

  // long double: 2トークン型名
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
    t = t->next->next;
    int sz = 8; // macOS/AArch64: long double == double (64-bit)
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }
  bool is_type = false;
  int scalar_size = 8;
  token_kind_t type_kind = t->kind;
  psx_ctx_get_type_info(type_kind, &is_type, &scalar_size);
  if (is_type) {
    t = t->next;
    // Extension: treat sizeof(void) as 1 (GNU-compatible behavior).
    int sz = (type_kind == TK_VOID) ? 1 : scalar_size;
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }
  if (t->kind == TK_STRUCT || t->kind == TK_UNION) {
    token_kind_t tag_kind = t->kind;
    set_curtok(t->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) return -1;
    int sz = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    if (sz <= 0) {
      psx_diag_undefined_with_name((token_t *)tag, diag_text_for(DIAG_TEXT_TAG_TYPE), tag->str, tag->len);
    }
    t = curtok();
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }
  if (psx_ctx_is_typedef_name_token(t)) {
    token_ident_t *id = (token_ident_t *)t;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_ptr = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len,
                              &td_ptr, NULL, NULL);
    t = t->next;
    int sz = td_ptr ? 8 : td_elem;
    while (t->kind == TK_MUL) {
      t = t->next;
      sz = 8;
    }
    int fp_ptr = 0;
    int fp_array_mul = 1;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
      sz = 8 * fp_array_mul;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
      sz = 8;
    }
    if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
      sz = 8;
    }
    set_curtok(t);
    apply_array_abstract_suffix_size(&sz);
    tk_expect(')');
    return sz;
  }
  return -1;
}
static node_t *parse_call_postfix(node_t *callee);

void psx_expr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind) {
  g_current_ret_token_kind = ret_kind;
  g_current_ret_fp_kind = fp_kind;
}

void psx_expr_set_current_func_ret_struct_size(int size) {
  g_current_ret_struct_size = size;
}

int psx_expr_current_func_ret_struct_size(void) {
  return g_current_ret_struct_size;
}

token_kind_t psx_expr_current_func_ret_token_kind(void) {
  return g_current_ret_token_kind;
}

tk_float_kind_t psx_expr_current_func_ret_fp_kind(void) {
  return g_current_ret_fp_kind;
}

void psx_expr_set_current_funcname(char *name, int len) {
  g_current_funcname = name;
  g_current_funcname_len = len;
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
  enter_expr_nest_or_die();
  node_t *node = assign();
  while (curtok()->kind == TK_COMMA) {
    set_curtok(curtok()->next);
    node_t *rhs = assign();
    node_t *comma = psx_node_new_binary(ND_COMMA, node, rhs);
    comma->fp_kind = rhs ? rhs->fp_kind : TK_FLOAT_KIND_NONE;
    node = comma;
  }
  leave_expr_nest();
  return node;
}

static node_t *apply_cast(token_kind_t type_kind, int is_pointer, node_t *operand) {
  if (is_pointer || type_kind == TK_LONG) {
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_STRUCT || type_kind == TK_UNION) {
    const char *kind = (type_kind == TK_STRUCT) ? "struct" : "union";
    psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                 kind);
  }
  if (type_kind == TK_FLOAT) {
    operand->fp_kind = TK_FLOAT_KIND_FLOAT;
    return operand;
  }
  if (type_kind == TK_DOUBLE) {
    operand->fp_kind = TK_FLOAT_KIND_DOUBLE;
    return operand;
  }
  if (type_kind == TK_INT || type_kind == TK_ENUM) {
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_SIGNED || type_kind == TK_UNSIGNED) {
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_BOOL) {
    return psx_node_new_binary(ND_NE, operand, psx_node_new_num(0));
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
  // Guard rail for unexpected parser state: known cast kinds should be handled above.
  psx_diag_ctx(curtok(), "cast", "%s",
               diag_message_for(DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED));
  return operand;
}

static node_t *assign(void) {
  node_t *node = conditional();
  switch (curtok()->kind) {
    case TK_ASSIGN: {
      psx_node_reject_const_assign(node, "=");
      set_curtok(curtok()->next);
      node_t *rhs = assign();
      psx_node_reject_const_qual_discard(node, rhs);
      if (node->kind == ND_LVAR) {
        lvar_t *lv = psx_decl_find_lvar_by_offset(((node_lvar_t *)node)->offset);
        if (lv) lv->is_initialized = 1;
      }
      node_mem_t *assign_node = psx_node_new_assign(node, rhs);
      assign_node->type_size = psx_node_type_size(assign_node->base.lhs);
      assign_node->base.fp_kind = assign_node->base.lhs ? assign_node->base.lhs->fp_kind : 0;
      node = (node_t *)assign_node;
      break;
    }
    case TK_PLUSEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_ADD, assign(), "+="); break;
    case TK_MINUSEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_SUB, assign(), "-="); break;
    case TK_MULEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_MUL, assign(), "*="); break;
    case TK_DIVEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_DIV, assign(), "/="); break;
    case TK_MODEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_MOD, assign(), "%="); break;
    case TK_SHLEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_SHL, assign(), "<<="); break;
    case TK_SHREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_SHR, assign(), ">>="); break;
    case TK_ANDEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_BITAND, assign(), "&="); break;
    case TK_XOREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_BITXOR, assign(), "^="); break;
    case TK_OREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(node, ND_BITOR, assign(), "|="); break;
    default: break;
  }
  return node;
}

static node_t *conditional(void) {
  node_t *node = logical_or();
  if (curtok()->kind == TK_QUESTION) {
    set_curtok(curtok()->next);
    node_ctrl_t *ternary = arena_alloc(sizeof(node_ctrl_t));
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
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_LOGOR, node, logical_and());
  }
  return node;
}

static node_t *logical_and(void) {
  node_t *node = bit_or();
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_LOGAND, node, bit_or());
  }
  return node;
}

static node_t *bit_or(void) {
  node_t *node = bit_xor();
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITOR, node, bit_xor());
  }
  return node;
}

static node_t *bit_xor(void) {
  node_t *node = bit_and();
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITXOR, node, bit_and());
  }
  return node;
}

static node_t *bit_and(void) {
  node_t *node = equality();
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITAND, node, equality());
  }
  return node;
}

static node_t *equality(void) {
  node_t *node = relational();
  for (;;) {
    if (curtok()->kind == TK_EQEQ) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_EQ, node, relational());
    } else if (curtok()->kind == TK_NEQ) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_NE, node, relational());
    }
    else return node;
  }
}

static node_t *relational(void) {
  node_t *node = shift();
  for (;;) {
    if (curtok()->kind == TK_LT) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LT, node, shift());
    } else if (curtok()->kind == TK_LE) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LE, node, shift());
    } else if (curtok()->kind == TK_GT) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LT, shift(), node);
    } else if (curtok()->kind == TK_GE) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LE, shift(), node);
    }
    else return node;
  }
}

static node_t *shift(void) {
  node_t *node = add();
  for (;;) {
    if (curtok()->kind == TK_SHL) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_SHL, node, add());
    } else if (curtok()->kind == TK_SHR) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_SHR, node, add());
    }
    else return node;
  }
}

static node_t *add(void) {
  node_t *node = mul();
  for (;;) {
    if (curtok()->kind == TK_PLUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul();
      if (psx_node_is_pointer(node)) {
        int ds = psx_node_deref_size(node);
        if (ds > 1) {
          // ポインタ + 整数: 整数を要素サイズ倍にスケーリング
          rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
        }
      }
      node = psx_node_new_binary(ND_ADD, node, rhs);
    } else if (curtok()->kind == TK_MINUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul();
      if (psx_node_is_pointer(node)) {
        int ds = psx_node_deref_size(node);
        if (ds > 1) {
          // ポインタ - 整数: 整数を要素サイズ倍にスケーリング
          rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
        }
      }
      node = psx_node_new_binary(ND_SUB, node, rhs);
    }
    else return node;
  }
}

static node_t *mul(void) {
  node_t *node = cast();
  for (;;) {
    if (curtok()->kind == TK_MUL) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_MUL, node, cast());
    } else if (curtok()->kind == TK_DIV) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_DIV, node, cast());
    } else if (curtok()->kind == TK_MOD) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_MOD, node, cast());
    }
    else return node;
  }
}

static node_t *cast(void) {
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  token_kind_t cast_tag_kind = TK_EOF;
  char *cast_tag_name = NULL;
  int cast_tag_len = 0;
  int cast_elem_size = 8;
  tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
  int cast_array_count = 0;
  if (parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind, &cast_array_count)) {
    if (after_rparen && after_rparen->kind == TK_LBRACE) {
      set_curtok(after_rparen);
      int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
      int is_arr = (!cast_is_ptr && cast_array_count > 0) ? 1 : 0;
      int var_size = cast_is_ptr ? 8 : (is_arr ? base_elem * cast_array_count : base_elem);
      char *tmp_name = new_compound_lit_name();

      if (g_current_funcname == NULL) {
        // ファイルスコープ: 静的ストレージ期間（C11 6.5.2.5p5）
        // スカラー型の場合は定数値として返す
        tk_expect('{');
        node_t *init_expr = psx_expr_assign();
        tk_expect('}');
        if (!is_arr && init_expr && init_expr->kind == ND_NUM) {
          return apply_postfix(init_expr);
        }
        // 配列・構造体等: グローバル変数として配置
        global_var_t *gv = calloc(1, sizeof(global_var_t));
        gv->name = tmp_name;
        gv->name_len = (int)strlen(tmp_name);
        gv->type_size = var_size;
        gv->deref_size = base_elem;
        gv->is_array = is_arr;
        if (init_expr && init_expr->kind == ND_NUM) {
          gv->has_init = 1;
          gv->init_val = ((node_num_t *)init_expr)->val;
        }
        gv->next = global_vars;
        global_vars = gv;
        node_gvar_t *gvar_node = arena_alloc(sizeof(node_gvar_t));
        gvar_node->mem.base.kind = ND_GVAR;
        gvar_node->mem.type_size = gv->type_size;
        gvar_node->mem.deref_size = gv->deref_size;
        gvar_node->name = gv->name;
        gvar_node->name_len = gv->name_len;
        gvar_node->is_thread_local = gv->is_thread_local;
        node_t *ref = (node_t *)gvar_node;
        return apply_postfix(ref);
      }

      // 関数スコープ: ブロックライフタイム（自動ストレージ期間）
      lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), var_size, base_elem, is_arr);
      var->tag_kind = cast_tag_kind;
      var->tag_name = cast_tag_name;
      var->tag_len = cast_tag_len;
      var->is_tag_pointer = cast_is_ptr ? 1 : 0;
      var->fp_kind = cast_fp_kind;
      node_t *init = psx_decl_parse_initializer_for_var(var, cast_is_ptr);
      node_t *ref;
      if (is_arr) {
        node_mem_t *addr_node = arena_alloc(sizeof(node_mem_t));
        addr_node->base.kind = ND_ADDR;
        addr_node->base.lhs = psx_node_new_lvar(var->offset);
        addr_node->type_size = var->elem_size;
        addr_node->deref_size = var->elem_size;
        ref = (node_t *)addr_node;
      } else {
        ref = new_typed_lvar_ref(var, cast_is_ptr);
      }
      node_t *val = apply_postfix(ref);
      return psx_node_new_binary(ND_COMMA, init, val);
    }
    set_curtok(after_rparen);
    node_t *operand = cast();
    if (!cast_is_ptr && (cast_kind == TK_STRUCT || cast_kind == TK_UNION)) {
      if (is_same_tag_nonscalar_expr(operand, cast_kind, cast_tag_name, cast_tag_len)) {
        // same-tag non-scalar cast: treat as no-op for now
        return apply_postfix(operand);
      }
      if (ps_get_enable_size_compatible_nonscalar_cast() &&
          is_size_compatible_nonscalar_expr(operand, cast_kind, cast_elem_size)) {
        // minimal extension: same-kind and same-size non-scalar cast as no-op
        return apply_postfix(operand);
      }
      if (cast_kind == TK_STRUCT) {
        token_kind_t op_tag_kind = TK_EOF;
        char *op_tag_name = NULL;
        int op_tag_len = 0;
        int op_is_tag_ptr = 0;
        psx_node_get_tag_type(operand, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
        if (!op_is_tag_ptr && (op_tag_kind == TK_STRUCT || op_tag_kind == TK_UNION)) {
          psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
                       "struct");
        }
        if (!ps_get_enable_struct_scalar_pointer_cast()) {
          psx_diag_ctx(curtok(), "cast", "%s",
                       diag_message_for(DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
        }
        return apply_postfix(lower_struct_value_cast(operand, cast_tag_kind, cast_tag_name, cast_tag_len,
                                                     cast_elem_size, cast_fp_kind));
      }
      if (cast_kind == TK_UNION) {
        token_kind_t op_tag_kind = TK_EOF;
        char *op_tag_name = NULL;
        int op_tag_len = 0;
        int op_is_tag_ptr = 0;
        psx_node_get_tag_type(operand, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
        if (!op_is_tag_ptr && (op_tag_kind == TK_STRUCT || op_tag_kind == TK_UNION)) {
          psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
                       "union");
        }
        if (!ps_get_enable_union_scalar_pointer_cast()) {
          psx_diag_ctx(curtok(), "cast", "%s",
                       diag_message_for(DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED));
        }
        // staged extension: allow scalar/pointer -> union value cast by
        // initializing the first union member, then yielding the union object.
        return apply_postfix(lower_union_value_cast(operand, cast_tag_kind, cast_tag_name, cast_tag_len,
                                                    cast_elem_size, cast_fp_kind));
      }
      const char *kind = (cast_kind == TK_STRUCT) ? "struct" : "union";
      psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                   kind);
    }
    return apply_postfix(apply_cast(cast_kind, cast_is_ptr, operand));
  }
  return unary();
}

static node_t *unary(void) {
  if (curtok()->kind == TK_SIZEOF) {
    set_curtok(curtok()->next);
    if (curtok()->kind == TK_LPAREN) {
      set_curtok(curtok()->next);
      int type_sz = parse_parenthesized_type_size();
      if (type_sz >= 0) return psx_node_new_num(type_sz);
      // VLA: sizeof(vla_var) は実行時サイズロード
      if (curtok()->kind == TK_IDENT) {
        token_ident_t *id = (token_ident_t *)curtok();
        lvar_t *vla_var = psx_decl_find_lvar(id->str, id->len);
        if (vla_var && vla_var->is_vla) {
          set_curtok(curtok()->next);
          tk_expect(')');
          // [x29+16+offset+8] にバイトサイズを格納してある
          return psx_node_new_lvar_typed(vla_var->offset + 8, 8);
        }
      }
      node_t *node = expr_internal();
      tk_expect(')');
      return psx_node_new_num(sizeof_expr_node(node));
    }
    return psx_node_new_num(sizeof_expr_node(unary()));
  }

  if (curtok()->kind == TK_ALIGNOF) {
    set_curtok(curtok()->next);
    tk_expect('(');
    int type_sz = parse_parenthesized_type_size();
    if (type_sz < 0) {
      psx_diag_ctx(curtok(), "alignof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED));
    }
    return psx_node_new_num(type_sz);
  }

  if (curtok()->kind == TK_INC) {
    set_curtok(curtok()->next);
    node_t *target = unary();
    psx_node_expect_incdec_target(target, "++");
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_PRE_INC;
    node->lhs = target;
    return node;
  }
  if (curtok()->kind == TK_DEC) {
    set_curtok(curtok()->next);
    node_t *target = unary();
    psx_node_expect_incdec_target(target, "--");
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_PRE_DEC;
    node->lhs = target;
    return node;
  }
  if (curtok()->kind == TK_PLUS) {
    set_curtok(curtok()->next);
    return cast();
  }
  if (curtok()->kind == TK_MINUS) {
    set_curtok(curtok()->next);
    return psx_node_new_binary(ND_SUB, psx_node_new_num(0), cast());
  }
  if (curtok()->kind == TK_BANG) {
    set_curtok(curtok()->next);
    return psx_node_new_binary(ND_EQ, cast(), psx_node_new_num(0));
  }
  if (curtok()->kind == TK_TILDE) {
    set_curtok(curtok()->next);
    node_t *neg = psx_node_new_binary(ND_SUB, psx_node_new_num(0), cast());
    return psx_node_new_binary(ND_SUB, neg, psx_node_new_num(1));
  }
  if (curtok()->kind == TK_MUL) {
    set_curtok(curtok()->next);
    node_t *operand = cast();
    node_mem_t *node = arena_alloc(sizeof(node_mem_t));
    node->base.kind = ND_DEREF;
    node->base.lhs = operand;
    node->base.fp_kind = TK_FLOAT_KIND_NONE;
    int ds = psx_node_deref_size(operand);
    node->type_size = ds ? ds : 8;
    token_kind_t tag_kind = TK_EOF;
    char *tag_name = NULL;
    int tag_len = 0;
    int is_tag_ptr = 0;
    psx_node_get_tag_type(operand, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
    if (tag_kind != TK_EOF && is_tag_ptr) {
      node->tag_kind = tag_kind;
      node->tag_name = tag_name;
      node->tag_len = tag_len;
      node->is_tag_pointer = 0;
      node->deref_size = 0;
    }
    // 多段ポインタ: *pp (int**) → int* なので is_pointer と deref_size を伝播
    int pql = psx_node_pointer_qual_levels(operand);
    tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(operand);
    if (pql == 1 && pointee_fp != TK_FLOAT_KIND_NONE) {
      node->base.fp_kind = pointee_fp;
    }
    if (pql >= 2) {
      node->is_pointer = 1;
      int new_pql = pql - 1;
      node->pointer_qual_levels = new_pql;
      int bds = psx_node_base_deref_size(operand);
      node->base_deref_size = (short)bds;
      node->deref_size = (new_pql >= 2) ? 8 : (short)bds;
    }
    return (node_t *)node;
  }
  if (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    node_t *operand = cast();
    if (operand && operand->kind == ND_COMMA && operand->rhs) {
      node_mem_t *rhs_addr = arena_alloc(sizeof(node_mem_t));
      rhs_addr->base.kind = ND_ADDR;
      rhs_addr->base.lhs = operand->rhs;
      token_kind_t rhs_tag_kind = TK_EOF;
      char *rhs_tag_name = NULL;
      int rhs_tag_len = 0;
      int rhs_is_tag_ptr = 0;
      psx_node_get_tag_type(rhs_addr->base.lhs, &rhs_tag_kind, &rhs_tag_name, &rhs_tag_len, &rhs_is_tag_ptr);
      if (rhs_tag_kind != TK_EOF && !rhs_is_tag_ptr) {
        rhs_addr->tag_kind = rhs_tag_kind;
        rhs_addr->tag_name = rhs_tag_name;
        rhs_addr->tag_len = rhs_tag_len;
        rhs_addr->is_tag_pointer = 1;
        rhs_addr->deref_size = psx_node_type_size(rhs_addr->base.lhs);
        rhs_addr->type_size = 8;
      }
      return psx_node_new_binary(ND_COMMA, operand->lhs, (node_t *)rhs_addr);
    }
    node_mem_t *node = arena_alloc(sizeof(node_mem_t));
    node->base.kind = ND_ADDR;
    node->base.lhs = operand;
    token_kind_t tag_kind = TK_EOF;
    char *tag_name = NULL;
    int tag_len = 0;
    int is_tag_ptr = 0;
    psx_node_get_tag_type(node->base.lhs, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
    if (tag_kind != TK_EOF && !is_tag_ptr) {
      node->tag_kind = tag_kind;
      node->tag_name = tag_name;
      node->tag_len = tag_len;
      node->is_tag_pointer = 1;
      node->deref_size = psx_node_type_size(node->base.lhs);
      node->type_size = 8;
    }
    return (node_t *)node;
  }

  node_t *node = primary();
  return apply_postfix(node);
}

static node_t *apply_postfix(node_t *node) {
  if (node && node->kind == ND_COMMA &&
      (curtok()->kind == TK_LBRACKET || curtok()->kind == TK_LPAREN ||
       curtok()->kind == TK_DOT || curtok()->kind == TK_ARROW ||
       curtok()->kind == TK_INC || curtok()->kind == TK_DEC)) {
    node->rhs = apply_postfix(node->rhs);
    return node;
  }

  for (;;) {
    if (curtok()->kind == TK_LBRACKET) {
      set_curtok(curtok()->next);
      node_t *idx = expr_internal();
      tk_expect(']');
      int ds = psx_node_deref_size(node);
      int ts = psx_node_type_size(node);
      int es = ds ? ds : (ts ? ts : 8);
      // 多次元VLA: 実行時ストライド・内側要素サイズを伝播
      int vla_rsf = 0;  // 実行時行ストライドのフレームオフセット (0=なし)
      int inner_ds = 0; // 次の次元の要素サイズ (0=スカラ)
      if (node->kind == ND_LVAR) {
        vla_rsf = as_lvar(node)->mem.vla_row_stride_frame_off;
        inner_ds = as_lvar(node)->mem.inner_deref_size;
      } else if (node->kind == ND_DEREF || node->kind == ND_ADDR) {
        inner_ds = ((node_mem_t *)node)->inner_deref_size;
      }
      node_t *scaled;
      if (vla_rsf) {
        // 実行時ストライド: フレームスロットから行ストライドをロード
        node_t *stride_node = psx_node_new_lvar_typed(vla_rsf, 8);
        scaled = psx_node_new_binary(ND_MUL, idx, stride_node);
        es = inner_ds ? inner_ds : 1; // type_size設定用 (実際は次段でderef_size経由で使用)
      } else {
        scaled = psx_node_new_binary(ND_MUL, idx, psx_node_new_num(es));
      }
      node_t *base_addr = node;
      if (node->kind == ND_DEREF) {
        node_mem_t *mem = (node_mem_t *)node;
        // 配列的アクセス: deref_size > 0 なら lhs (配列ベースアドレス) を直接使用
        // (struct メンバ配列・多次元VLA サブスクリプト共通)
        if (mem->deref_size > 0 && !mem->is_pointer) {
          base_addr = node->lhs;
        } else if (node->lhs && node->lhs->kind == ND_ADD &&
                   node->lhs->rhs && node->lhs->rhs->kind == ND_NUM) {
          // Member lvalue (`s.m`) is represented as `*(base + off)`.
          // Indexing should start from `base + off`, not from loaded `m` value.
          base_addr = node->lhs;
        }
      }
      node_t *addr = psx_node_new_binary(ND_ADD, base_addr, scaled);
      node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
      deref->base.kind = ND_DEREF;
      deref->base.lhs = addr;
      deref->type_size = es;
      deref->deref_size = inner_ds; // 多次元配列: 次段のストライド (0=スカラ)
      deref->base.fp_kind = TK_FLOAT_KIND_NONE;
      token_kind_t tag_kind = TK_EOF;
      char *tag_name = NULL;
      int tag_len = 0;
      int is_tag_ptr = 0;
      psx_node_get_tag_type(node, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
      if (tag_kind != TK_EOF && is_tag_ptr) {
        deref->tag_kind = tag_kind;
        deref->tag_name = tag_name;
        deref->tag_len = tag_len;
        deref->is_tag_pointer = 0;
      }
      // 配列要素がポインタ型の場合: サブスクリプト結果にポインタ情報を伝播
      {
        int pql = psx_node_pointer_qual_levels(node);
        if (pql >= 1) {
          deref->is_pointer = 1;
          deref->pointer_qual_levels = pql;
          int bds = psx_node_base_deref_size(node);
          deref->base_deref_size = (short)bds;
          deref->deref_size = (pql >= 2) ? 8 : (short)bds;
        }
        if (pql == 1) {
          tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(node);
          if (pointee_fp != TK_FLOAT_KIND_NONE) {
            deref->base.fp_kind = pointee_fp;
          }
        }
      }
      node = (node_t *)deref;
      continue;
    }
    if (curtok()->kind == TK_LPAREN) {
      node = parse_call_postfix(node);
      continue;
    }
    if (curtok()->kind == TK_DOT) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node = build_member_access(node, 0, op_tok);
      continue;
    }
    if (curtok()->kind == TK_ARROW) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node = build_member_access(node, 1, op_tok);
      continue;
    }
    if (curtok()->kind == TK_INC) {
      set_curtok(curtok()->next);
      psx_node_expect_incdec_target(node, "++");
      node_t *inc = arena_alloc(sizeof(node_t));
      inc->kind = ND_POST_INC;
      inc->lhs = node;
      node = inc;
      continue;
    }
    if (curtok()->kind == TK_DEC) {
      set_curtok(curtok()->next);
      psx_node_expect_incdec_target(node, "--");
      node_t *dec = arena_alloc(sizeof(node_t));
      dec->kind = ND_POST_DEC;
      dec->lhs = node;
      node = dec;
      continue;
    }
    break;
  }
  return node;
}

static node_t *parse_call_postfix(node_t *callee) {
  tk_expect('(');
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  node->callee = callee;
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign();
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
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

static node_t *primary(void) {
  if (curtok()->kind == TK_GENERIC) {
    set_curtok(curtok()->next);
    tk_expect('(');
    node_t *control = assign();
    generic_type_t control_ty = infer_generic_control_type(control);
    tk_expect(',');

    node_t *selected = NULL;
    node_t *default_expr = NULL;
    int matched = 0;
    for (;;) {
      if (curtok()->kind == TK_DEFAULT) {
        set_curtok(curtok()->next);
        tk_expect(':');
        node_t *expr_node = assign();
        if (!default_expr) default_expr = expr_node;
      } else {
        generic_type_t assoc_ty = {TK_EOF, 0};
        if (!parse_generic_assoc_type(&assoc_ty)) {
          psx_diag_ctx(curtok(), "generic", "%s",
                       diag_message_for(DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID));
        }
        tk_expect(':');
        node_t *expr_node = assign();
        if (!matched && generic_type_matches(control_ty, assoc_ty)) {
          selected = expr_node;
          matched = 1;
        }
      }
      if (!tk_consume(',')) break;
    }
    tk_expect(')');
    if (!selected) selected = default_expr;
    if (!selected) {
      psx_diag_ctx(curtok(), "generic", "%s",
                   diag_message_for(DIAG_ERR_PARSER_GENERIC_NO_MATCH));
    }
    return selected;
  }

  if (curtok()->kind == TK_NUM) {
    token_t *tok = curtok();
    token_num_t *num = (token_num_t *)tok;
    node_num_t *node = arena_alloc(sizeof(node_num_t));
    node->base.kind = ND_NUM;
    if (num->num_kind == TK_NUM_KIND_INT) {
      node->base.fp_kind = TK_FLOAT_KIND_NONE;
      node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      node->val = tk_as_num_int(tok)->val;
    } else {
      node->base.fp_kind = tk_as_num_float(tok)->fp_kind;
      node->float_suffix_kind = tk_as_num_float(tok)->float_suffix_kind;
      node->fval = tk_as_num_float(tok)->fval;
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

    set_curtok(curtok()->next);
    return (node_t *)node;
  }

  if (curtok()->kind == TK_LPAREN) {
    enter_paren_nest_or_die();
    set_curtok(curtok()->next);
    node_t *node = expr_internal();
    tk_expect(')');
    leave_paren_nest();
    return node;
  }

  token_ident_t *tok = tk_consume_ident();
  if (tok) {
    // __func__: C11 6.4.2.2 — 各関数本体に暗黙定義される const char[] の関数名
    if (tok->len == 8 && memcmp(tok->str, "__func__", 8) == 0) {
      const char *fname = g_current_funcname ? g_current_funcname : "";
      int flen = g_current_funcname ? g_current_funcname_len : 0;
      char *fstr = calloc((size_t)flen + 1, 1);
      memcpy(fstr, fname, (size_t)flen);
      node_string_t *snode = arena_alloc(sizeof(node_string_t));
      snode->mem.base.kind = ND_STRING;
      int id = string_label_count++;
      int label_len = snprintf(NULL, 0, ".LC%d", id);
      snode->string_label = calloc((size_t)label_len + 1, 1);
      snprintf(snode->string_label, (size_t)label_len + 1, ".LC%d", id);
      string_lit_t *lit = calloc(1, sizeof(string_lit_t));
      lit->label = snode->string_label;
      lit->str = fstr;
      lit->len = flen;
      lit->char_width = TK_CHAR_WIDTH_CHAR;
      lit->str_prefix_kind = TK_STR_PREFIX_NONE;
      lit->next = string_literals;
      string_literals = lit;
      snode->mem.type_size = 8;
      snode->mem.deref_size = 1;
      snode->mem.base.fp_kind = TK_FLOAT_KIND_NONE;
      snode->char_width = TK_CHAR_WIDTH_CHAR;
      snode->str_prefix_kind = TK_STR_PREFIX_NONE;
      return (node_t *)snode;
    }
    lvar_t *var = psx_decl_find_lvar(tok->str, tok->len);
    if (!var) {
      long long enum_val = 0;
      if (psx_ctx_find_enum_const(tok->str, tok->len, &enum_val)) {
        return psx_node_new_num(enum_val);
      }
    }
    if (curtok()->kind == TK_LPAREN) {
      if (!var) {
        set_curtok(curtok()->next);
        node_func_t *node = arena_alloc(sizeof(node_func_t));
        node->base.kind = ND_FUNCALL;
        node->callee = NULL;
        node->funcname = tok->str;
        node->funcname_len = tok->len;
        int nargs = 0;
        int arg_cap = 16;
        node->args = calloc(arg_cap, sizeof(node_t*));
        if (curtok()->kind == TK_RPAREN) {
          set_curtok(curtok()->next);
        } else {
          node->args[nargs++] = assign();
          while (curtok()->kind == TK_COMMA) {
            set_curtok(curtok()->next);
            if (nargs >= arg_cap) {
              arg_cap = pda_next_cap(arg_cap, nargs + 1);
              node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
            }
            node->args[nargs++] = assign();
          }
          tk_expect(')');
        }
        node->nargs = nargs;
        node->base.ret_struct_size = psx_ctx_get_function_ret_struct_size(
            tok->str, tok->len);
        return (node_t *)node;
      }
    }

    if (!var && psx_ctx_has_function_name(tok->str, tok->len)) {
      node_funcref_t *fr = arena_alloc(sizeof(node_funcref_t));
      fr->base.kind = ND_FUNCREF;
      fr->funcname = tok->str;
      fr->funcname_len = tok->len;
      return (node_t *)fr;
    }

    // グローバル変数テーブルを検索
    if (!var) {
      for (global_var_t *gv = global_vars; gv; gv = gv->next) {
        if (gv->name_len == tok->len && memcmp(gv->name, tok->str, (size_t)tok->len) == 0) {
          if (gv->is_array) {
            // グローバル配列: アドレスをND_ADDRとして返す（ローカル配列と同様）
            node_gvar_t *base = arena_alloc(sizeof(node_gvar_t));
            base->mem.base.kind = ND_GVAR;
            base->mem.type_size = gv->type_size;
            base->mem.deref_size = gv->deref_size;
            base->name = gv->name;
            base->name_len = gv->name_len;
            base->is_thread_local = gv->is_thread_local;
            node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
            addr->base.kind = ND_ADDR;
            addr->base.lhs = (node_t *)base;
            addr->type_size = gv->deref_size;
            addr->deref_size = gv->deref_size;
            addr->is_pointer = 1;
            return (node_t *)addr;
          }
          node_gvar_t *gvar_node = arena_alloc(sizeof(node_gvar_t));
          gvar_node->mem.base.kind = ND_GVAR;
          gvar_node->mem.type_size = gv->type_size;
          gvar_node->mem.deref_size = gv->deref_size;
          gvar_node->name = gv->name;
          gvar_node->name_len = gv->name_len;
          gvar_node->is_thread_local = gv->is_thread_local;
          return (node_t *)gvar_node;
        }
      }
    }

    if (!var) {
      var = psx_decl_register_lvar(tok->str, tok->len);
    }
    var->is_used = 1;
    if (var->is_array && !var->is_vla) {
      node_mem_t *node = arena_alloc(sizeof(node_mem_t));
      node->base.kind = ND_ADDR;
      node->base.lhs = psx_node_new_lvar(var->offset);
      int stride = (var->outer_stride > 0) ? var->outer_stride : var->elem_size;
      node->type_size = stride;
      node->deref_size = stride;
      node->pointee_fp_kind = var->pointee_fp_kind;
      if (var->outer_stride > 0) node->inner_deref_size = var->elem_size;
      node->tag_kind = var->tag_kind;
      node->tag_name = var->tag_name;
      node->tag_len = var->tag_len;
      node->is_tag_pointer = (var->tag_kind != TK_EOF) ? 1 : 0;
      node->is_pointer = 1;
      node->pointer_qual_levels = var->pointer_qual_levels;
      node->base_deref_size = var->base_deref_size;
      return (node_t *)node;
    }
    // byref仮引数 (>16バイト構造体の値渡し): フレームスロットからポインタを読み込み、
    // ND_DEREF でラップして「struct値」として見せる。
    // p.member → build_member_access(ND_DEREF(ptr_lvar), from_ptr=0)
    //  → ND_ADDR(ND_DEREF(ptr_lvar)) = struct base ptr → offset → deref → member ✓
    if (var->is_byref_param) {
      node_t *ptr_lvar = psx_node_new_lvar_typed(var->offset, 8);  // loads ptr from frame
      node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
      deref->base.kind = ND_DEREF;
      deref->base.lhs = ptr_lvar;
      deref->type_size = var->elem_size;  // 実際の構造体サイズ
      deref->tag_kind = var->tag_kind;
      deref->tag_name = var->tag_name;
      deref->tag_len = var->tag_len;
      deref->is_tag_pointer = 0;  // 値（構造体）であってポインタではない
      return (node_t *)deref;
    }
    // VLA: フレームスロットからベースポインタを読み込む (ポインタ変数として扱う)
    node_t *n = psx_node_new_lvar_typed(var->offset, var->is_array ? 8 : (var->size > var->elem_size ? 8 : var->elem_size));
    // 多次元VLA: outer_strideが設定されていれば外側サブスクリプトストライドとして使用
    // runtime inner (outer_stride=0): deref_sizeは0のまま (vla_row_stride_frame_offで実行時参照)
    int vla_effective_deref = (var->outer_stride > 0) ? var->outer_stride : (var->vla_row_stride_frame_off ? 0 : var->elem_size);
    as_lvar(n)->mem.deref_size = vla_effective_deref;
    // 2D VLA: サブスクリプト結果の要素サイズ (次の次元のstride, 0=スカラ)
    int vla_is_multidim = (var->outer_stride != var->elem_size) || (var->vla_row_stride_frame_off != 0);
    as_lvar(n)->mem.inner_deref_size = vla_is_multidim ? var->elem_size : 0;
    as_lvar(n)->mem.vla_row_stride_frame_off = var->vla_row_stride_frame_off;
    as_lvar(n)->mem.tag_kind = var->tag_kind;
    as_lvar(n)->mem.tag_name = var->tag_name;
    as_lvar(n)->mem.tag_len = var->tag_len;
    as_lvar(n)->mem.is_tag_pointer = var->is_tag_pointer;
    as_lvar(n)->mem.is_pointer = var->is_array || (var->size > var->elem_size);
    as_lvar(n)->mem.is_const_qualified = var->is_const_qualified;
    as_lvar(n)->mem.is_volatile_qualified = var->is_volatile_qualified;
    as_lvar(n)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
    as_lvar(n)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
    as_lvar(n)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
    as_lvar(n)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
    as_lvar(n)->mem.pointer_qual_levels = var->pointer_qual_levels;
    as_lvar(n)->mem.base_deref_size = var->base_deref_size;
    as_lvar(n)->mem.pointee_fp_kind = var->pointee_fp_kind;
    as_lvar(n)->mem.is_unsigned = var->is_unsigned;
    as_lvar(n)->mem.is_complex = var->is_complex;
    as_lvar(n)->mem.is_atomic = var->is_atomic;
    n->is_complex = var->is_complex;
    n->is_atomic = var->is_atomic;
    n->fp_kind = var->fp_kind;
    return n;
  }

  if (curtok()->kind == TK_STRING) {
    tk_char_width_t merged_width = TK_CHAR_WIDTH_CHAR;
    tk_string_prefix_kind_t merged_prefix_kind = TK_STR_PREFIX_NONE;
    size_t total_len = 0;
    token_t *t = curtok();
    while (t && t->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)t;
      if (total_len == 0) {
        merged_width = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
        merged_prefix_kind = st->str_prefix_kind;
      } else if (merged_width != st->char_width) {
        diag_emit_tokf(DIAG_ERR_PARSER_UNEXPECTED_TOKEN, t,
                       "%s",
                       diag_message_for(DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH));
      }
      if (st->len < 0 || (size_t)st->len > SIZE_MAX - total_len - 1) {
        diag_emit_tokf(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, t, "%s",
                       diag_message_for(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
      }
      total_len += (size_t)st->len;
      t = t->next;
    }

    if (total_len > (size_t)INT_MAX) {
      diag_emit_tokf(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
    }
    char *merged = calloc(total_len + 1, 1);
    if (!merged) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    size_t off = 0;
    while (curtok() && curtok()->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)curtok();
      if (st->len < 0 || (size_t)st->len > total_len - off) {
        diag_emit_tokf(DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID, curtok(), "%s",
                       diag_message_for(DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID));
      }
      memcpy(merged + off, st->str, (size_t)st->len);
      off += (size_t)st->len;
      set_curtok(curtok()->next);
    }

    node_string_t *node = arena_alloc(sizeof(node_string_t));
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

  psx_diag_ctx(curtok(), "primary", "%s",
               diag_message_for(DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
  return NULL;
}
