#include "internal/expr.h"
#include "internal/core.h"
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
static int compound_lit_seq = 0;

static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

typedef struct {
  token_kind_t kind;
  int is_pointer;
} generic_type_t;

static int sizeof_expr_node(node_t *node) {
  int sz = psx_node_type_size(node);
  if (sz) return sz;
  if (node && node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (node && node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 8;
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

static int parse_funcptr_abstract_decl(token_t **ptok, int *is_pointer) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
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

static void skip_ptr_qualifiers_expr(void) {
  while (token->kind == TK_CONST || token->kind == TK_VOLATILE || token->kind == TK_RESTRICT) {
    token = token->next;
  }
}

static void consume_local_type_quals(token_t **cur) {
  while (*cur && ((*cur)->kind == TK_CONST || (*cur)->kind == TK_VOLATILE || (*cur)->kind == TK_RESTRICT)) {
    *cur = (*cur)->next;
  }
}

static generic_type_t infer_generic_control_type(node_t *control) {
  generic_type_t gt = {TK_INT, 0};
  if (!control) return gt;
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
  if (control.is_pointer) return 1;
  return control.kind == assoc.kind;
}

static int parse_generic_assoc_type(generic_type_t *out) {
  out->kind = TK_EOF;
  out->is_pointer = 0;
  if (psx_ctx_is_typedef_name_token(token)) {
    token_ident_t *id = (token_ident_t *)token;
    token_kind_t base_kind = TK_EOF;
    int elem_size = 8;
    tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
    token_kind_t tag_kind = TK_EOF;
    char *tag_name = NULL;
    int tag_len = 0;
    int is_ptr = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_ptr);
    token = token->next;
    out->kind = base_kind;
    out->is_pointer = is_ptr;
  } else {
    token_kind_t tk = psx_consume_type_kind();
    if (tk == TK_EOF) return 0;
    out->kind = tk;
  }
  while (tk_consume('*')) {
    out->is_pointer = 1;
    skip_ptr_qualifiers_expr();
  }
  return 1;
}

static node_t *build_member_access(node_t *base, int from_ptr, token_t *op_tok) {
  token_ident_t *member = tk_consume_ident();
  if (!member) {
    psx_diag_missing(token, "メンバ名");
  }

  token_kind_t base_tag_kind = TK_EOF;
  char *base_tag_name = NULL;
  int base_tag_len = 0;
  int base_is_ptr = 0;
  psx_node_get_tag_type(base, &base_tag_kind, &base_tag_name, &base_tag_len, &base_is_ptr);
  if (base_tag_kind == TK_EOF || (!from_ptr && base_is_ptr) || (from_ptr && !base_is_ptr)) {
    tk_error_tok(op_tok, from_ptr ? "'->' の左辺は構造体/共用体ポインタである必要があります"
                                  : "'.' の左辺は構造体/共用体である必要があります");
  }

  int off = 0, mem_size = 0, mem_deref = 0;
  token_kind_t mem_tag_kind = TK_EOF;
  char *mem_tag_name = NULL;
  int mem_tag_len = 0;
  int mem_is_ptr = 0;
  if (!psx_ctx_find_tag_member(base_tag_kind, base_tag_name, base_tag_len,
                               member->str, member->len,
                               &off, &mem_size, &mem_deref, NULL,
                               &mem_tag_kind, &mem_tag_name, &mem_tag_len, &mem_is_ptr)) {
    psx_diag_ctx(op_tok, "member", "メンバ '%.*s' は存在しません", member->len, member->str);
  }

  node_t *addr_base = base;
  if (!from_ptr) {
    node_mem_t *addr = calloc(1, sizeof(node_mem_t));
    addr->base.kind = ND_ADDR;
    addr->base.lhs = base;
    addr->type_size = 8;
    addr_base = (node_t *)addr;
  }
  node_t *addr = psx_node_new_binary(ND_ADD, addr_base, psx_node_new_num(off));
  node_mem_t *deref = calloc(1, sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  deref->type_size = mem_size ? mem_size : 8;
  deref->deref_size = mem_deref;
  deref->tag_kind = mem_tag_kind;
  deref->tag_name = mem_tag_name;
  deref->tag_len = mem_tag_len;
  deref->is_tag_pointer = mem_is_ptr;
  return (node_t *)deref;
}

static int parse_cast_type(token_t *tok, token_kind_t *type_kind, int *is_pointer, token_t **after_rparen,
                           token_kind_t *out_tag_kind, char **out_tag_name, int *out_tag_len,
                           int *out_elem_size, tk_float_kind_t *out_fp_kind) {
  if (!tok || tok->kind != TK_LPAREN) return 0;
  token_t *t = tok->next;
  if (!t) return 0;
  *type_kind = TK_EOF;
  if (out_tag_kind) *out_tag_kind = TK_EOF;
  if (out_tag_name) *out_tag_name = NULL;
  if (out_tag_len) *out_tag_len = 0;
  if (out_elem_size) *out_elem_size = 8;
  if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_NONE;

  consume_local_type_quals(&t);
  if (t && (t->kind == TK_THREAD_LOCAL || t->kind == TK_EXTERN || t->kind == TK_STATIC ||
            t->kind == TK_AUTO || t->kind == TK_REGISTER || t->kind == TK_TYPEDEF)) {
    psx_diag_ctx(t, "cast", "cast 型名にストレージ指定子は使えません");
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
        psx_diag_undefined_with_name(q, "のタグ型", tag->str, tag->len);
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
      psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len, &td_ptr);
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
    while (q && q->kind == TK_MUL) {
      inner_ptr = 1;
      q = q->next;
    }
    consume_local_type_quals(&q);
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
      tk_error_tok(t, "_Complex/_Imaginary cast は浮動小数型のみ対応です");
    }
  } else if ((t->kind == TK_FLOAT || t->kind == TK_DOUBLE || t->kind == TK_LONG) &&
             t->next && (t->next->kind == TK_COMPLEX || t->next->kind == TK_IMAGINARY)) {
    if (t->kind == TK_LONG) {
      if (!t->next || t->next->kind != TK_DOUBLE || !t->next->next ||
          (t->next->next->kind != TK_COMPLEX && t->next->next->kind != TK_IMAGINARY)) {
        tk_error_tok(t, "_Complex/_Imaginary cast は浮動小数型のみ対応です");
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
      *type_kind = t->kind;
      if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
      if (out_fp_kind) {
        if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
        else if (*type_kind == TK_DOUBLE) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      }
      t = t->next;
    }
  } else if (psx_ctx_is_tag_keyword(t->kind)) {
    token_kind_t tag_kind = t->kind;
    t = t->next;
    token_ident_t *tag = (token_ident_t *)t;
    if (!t || t->kind != TK_IDENT) return 0;
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name(t, "のタグ型", tag->str, tag->len);
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
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len, &td_ptr);
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
  consume_local_type_quals(&t);
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
  }
  consume_local_type_quals(&t);
  parse_funcptr_abstract_decl(&t, is_pointer);
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
static node_t *apply_postfix(node_t *node);

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
  return ref;
}

static int parse_parenthesized_type_size(void) {
  // Minimal support for C11 complex/imaginary spellings in sizeof/alignof:
  //   _Complex float, _Imaginary double, float _Complex, double _Imaginary
  if (token->kind == TK_COMPLEX || token->kind == TK_IMAGINARY) {
    token = token->next;
    int sz = 0;
    if (token->kind == TK_FLOAT) {
      sz = 4;
      token = token->next;
    } else if (token->kind == TK_DOUBLE) {
      sz = 8;
      token = token->next;
    } else if (token->kind == TK_LONG && token->next && token->next->kind == TK_DOUBLE) {
      sz = 8;
      token = token->next->next;
    } else {
      return -1;
    }
    while (token->kind == TK_MUL) {
      token = token->next;
      sz = 8;
    }
    int fp_ptr = 0;
    if (parse_funcptr_abstract_decl(&token, &fp_ptr)) {
      sz = 8;
    }
    tk_expect(')');
    return sz;
  }
  if ((token->kind == TK_FLOAT || token->kind == TK_DOUBLE) &&
      token->next && (token->next->kind == TK_COMPLEX || token->next->kind == TK_IMAGINARY)) {
    int sz = (token->kind == TK_FLOAT) ? 4 : 8;
    token = token->next->next;
    while (token->kind == TK_MUL) {
      token = token->next;
      sz = 8;
    }
    int fp_ptr = 0;
    if (parse_funcptr_abstract_decl(&token, &fp_ptr)) {
      sz = 8;
    }
    tk_expect(')');
    return sz;
  }
  if (token->kind == TK_LONG && token->next && token->next->kind == TK_DOUBLE &&
      token->next->next &&
      (token->next->next->kind == TK_COMPLEX || token->next->next->kind == TK_IMAGINARY)) {
    int sz = 8;
    token = token->next->next->next;
    while (token->kind == TK_MUL) {
      token = token->next;
      sz = 8;
    }
    int fp_ptr = 0;
    if (parse_funcptr_abstract_decl(&token, &fp_ptr)) {
      sz = 8;
    }
    tk_expect(')');
    return sz;
  }

  bool is_type = false;
  int scalar_size = 8;
  token_kind_t type_kind = token->kind;
  psx_ctx_get_type_info(type_kind, &is_type, &scalar_size);
  if (is_type) {
    token = token->next;
    // Extension: treat sizeof(void) as 1 (GNU-compatible behavior).
    int sz = (type_kind == TK_VOID) ? 1 : scalar_size;
    while (token->kind == TK_MUL) {
      token = token->next;
      sz = 8;
    }
    int fp_ptr = 0;
    if (parse_funcptr_abstract_decl(&token, &fp_ptr)) {
      sz = 8;
    }
    tk_expect(')');
    return sz;
  }
  if (psx_ctx_is_typedef_name_token(token)) {
    token_ident_t *id = (token_ident_t *)token;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_ptr = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len, &td_ptr);
    token = token->next;
    int sz = td_ptr ? 8 : td_elem;
    while (token->kind == TK_MUL) {
      token = token->next;
      sz = 8;
    }
    int fp_ptr = 0;
    if (parse_funcptr_abstract_decl(&token, &fp_ptr)) {
      sz = 8;
    }
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
  if (type_kind == TK_STRUCT || type_kind == TK_UNION) {
    const char *kind = (type_kind == TK_STRUCT) ? "struct" : "union";
    psx_diag_ctx(token, "cast", "%s 値へのキャストは未対応です（非スカラ型）", kind);
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
  token_kind_t cast_tag_kind = TK_EOF;
  char *cast_tag_name = NULL;
  int cast_tag_len = 0;
  int cast_elem_size = 8;
  tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
  if (parse_cast_type(token, &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind)) {
    if (after_rparen && after_rparen->kind == TK_LBRACE) {
      token = after_rparen;
      // Compound literal strategy (minimal):
      // materialize as a hidden local stack object and lower to
      //   (init(hidden_obj), hidden_obj)
      // This gives block-lifetime behavior in function scope for current backend.
      int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
      int var_size = cast_is_ptr ? 8 : base_elem;
      char *tmp_name = new_compound_lit_name();
      lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), var_size, base_elem, 0);
      var->tag_kind = cast_tag_kind;
      var->tag_name = cast_tag_name;
      var->tag_len = cast_tag_len;
      var->is_tag_pointer = cast_is_ptr ? 1 : 0;
      var->fp_kind = cast_fp_kind;
      node_t *init = psx_decl_parse_initializer_for_var(var, cast_is_ptr);
      node_t *ref = new_typed_lvar_ref(var, cast_is_ptr);
      node_t *val = apply_postfix(ref);
      return psx_node_new_binary(ND_COMMA, init, val);
    }
    token = after_rparen;
    node_t *operand = unary();
    if (!cast_is_ptr && (cast_kind == TK_STRUCT || cast_kind == TK_UNION)) {
      node_t *cast_src = operand;
      while (cast_src && cast_src->kind == ND_COMMA) cast_src = cast_src->rhs;
      token_kind_t op_tag_kind = TK_EOF;
      char *op_tag_name = NULL;
      int op_tag_len = 0;
      int op_is_tag_ptr = 0;
      psx_node_get_tag_type(cast_src, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
      if (!op_is_tag_ptr && op_tag_kind == cast_kind && op_tag_len == cast_tag_len &&
          strncmp(op_tag_name ? op_tag_name : "", cast_tag_name ? cast_tag_name : "",
                  (size_t)cast_tag_len) == 0) {
        // same-tag non-scalar cast: treat as no-op for now
        return operand;
      }
      const char *kind = (cast_kind == TK_STRUCT) ? "struct" : "union";
      psx_diag_ctx(token, "cast", "%s 値へのキャストは未対応です（非スカラ型）", kind);
    }
    return apply_cast(cast_kind, cast_is_ptr, operand);
  }

  if (token->kind == TK_SIZEOF) {
    token = token->next;
    if (token->kind == TK_LPAREN) {
      token = token->next;
      int type_sz = parse_parenthesized_type_size();
      if (type_sz >= 0) return psx_node_new_num(type_sz);
      node_t *node = expr_internal();
      tk_expect(')');
      return psx_node_new_num(sizeof_expr_node(node));
    }
    return psx_node_new_num(sizeof_expr_node(unary()));
  }

  if (token->kind == TK_ALIGNOF) {
    token = token->next;
    tk_expect('(');
    int type_sz = parse_parenthesized_type_size();
    if (type_sz < 0) {
      psx_diag_ctx(token, "alignof", "_Alignof には型名が必要です");
    }
    return psx_node_new_num(type_sz);
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
    return (node_t *)node;
  }
  if (token->kind == TK_AMP) {
    token = token->next;
    node_mem_t *node = calloc(1, sizeof(node_mem_t));
    node->base.kind = ND_ADDR;
    node->base.lhs = unary();
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
      (token->kind == TK_LBRACKET || token->kind == TK_LPAREN ||
       token->kind == TK_DOT || token->kind == TK_ARROW ||
       token->kind == TK_INC || token->kind == TK_DEC)) {
    node->rhs = apply_postfix(node->rhs);
    return node;
  }

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
    node = (node_t *)deref;
  }
  for (;;) {
    if (token->kind == TK_LPAREN) {
      node = parse_call_postfix(node);
      continue;
    }
    if (token->kind == TK_DOT) {
      token_t *op_tok = token;
      token = token->next;
      node = build_member_access(node, 0, op_tok);
      continue;
    }
    if (token->kind == TK_ARROW) {
      token_t *op_tok = token;
      token = token->next;
      node = build_member_access(node, 1, op_tok);
      continue;
    }
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

static node_t *parse_call_postfix(node_t *callee) {
  tk_expect('(');
  node_func_t *node = calloc(1, sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  node->callee = callee;
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
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

static node_t *primary(void) {
  if (token->kind == TK_GENERIC) {
    token = token->next;
    tk_expect('(');
    node_t *control = assign();
    generic_type_t control_ty = infer_generic_control_type(control);
    tk_expect(',');

    node_t *selected = NULL;
    node_t *default_expr = NULL;
    int matched = 0;
    for (;;) {
      if (token->kind == TK_DEFAULT) {
        token = token->next;
        tk_expect(':');
        node_t *expr_node = assign();
        if (!default_expr) default_expr = expr_node;
      } else {
        generic_type_t assoc_ty = {TK_EOF, 0};
        if (!parse_generic_assoc_type(&assoc_ty)) {
          psx_diag_ctx(token, "generic", "_Generic の関連型が不正です");
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
      psx_diag_ctx(token, "generic", "_Generic に一致する関連がありません");
    }
    return selected;
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
    lvar_t *var = psx_decl_find_lvar(tok->str, tok->len);
    if (!var) {
      long long enum_val = 0;
      if (psx_ctx_find_enum_const(tok->str, tok->len, &enum_val)) {
        return psx_node_new_num(enum_val);
      }
    }
    if (token->kind == TK_LPAREN) {
      if (!var) {
        token = token->next;
        node_func_t *node = calloc(1, sizeof(node_func_t));
        node->base.kind = ND_FUNCALL;
        node->callee = NULL;
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
    }

    if (!var && psx_ctx_has_function_name(tok->str, tok->len)) {
      node_funcref_t *fr = calloc(1, sizeof(node_funcref_t));
      fr->base.kind = ND_FUNCREF;
      fr->funcname = tok->str;
      fr->funcname_len = tok->len;
      return (node_t *)fr;
    }

    if (!var) {
      var = psx_decl_register_lvar(tok->str, tok->len);
    }
    if (var->is_array) {
      node_mem_t *node = calloc(1, sizeof(node_mem_t));
      node->base.kind = ND_ADDR;
      node->base.lhs = psx_node_new_lvar(var->offset - var->size + var->elem_size);
      node->type_size = var->elem_size;
      node->deref_size = var->elem_size;
      node->tag_kind = var->tag_kind;
      node->tag_name = var->tag_name;
      node->tag_len = var->tag_len;
      node->is_tag_pointer = (var->tag_kind != TK_EOF) ? 1 : 0;
      return (node_t *)node;
    }
    node_t *n = psx_node_new_lvar_typed(var->offset, var->is_array ? 8 : (var->size > var->elem_size ? 8 : var->elem_size));
    as_lvar(n)->mem.deref_size = var->elem_size;
    as_lvar(n)->mem.tag_kind = var->tag_kind;
    as_lvar(n)->mem.tag_name = var->tag_name;
    as_lvar(n)->mem.tag_len = var->tag_len;
    as_lvar(n)->mem.is_tag_pointer = var->is_tag_pointer;
    as_lvar(n)->mem.is_const_qualified = var->is_const_qualified;
    as_lvar(n)->mem.is_volatile_qualified = var->is_volatile_qualified;
    as_lvar(n)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
    as_lvar(n)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
    as_lvar(n)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
    as_lvar(n)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
    as_lvar(n)->mem.pointer_qual_levels = var->pointer_qual_levels;
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
