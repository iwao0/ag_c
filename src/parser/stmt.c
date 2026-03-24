#include "internal/stmt.h"
#include "internal/arena.h"
#include "internal/core.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/switch_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>

node_t *ps_expr(void);

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

static int parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);
static void parse_typedef_decl(void);
static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind);
static token_ident_t *parse_typedef_name_decl(int *is_ptr);
static token_ident_t *parse_typedef_name_decl_recursive(int *is_ptr);
static token_ident_t *parse_member_decl_name_recursive_stmt(int *is_ptr, int *out_has_func_suffix);
static long long parse_enum_const_expr(void);
static long long parse_enum_const_conditional(void);
static long long parse_enum_const_logor(void);
static long long parse_enum_const_logand(void);
static long long parse_enum_const_bitor(void);
static long long parse_enum_const_bitxor(void);
static long long parse_enum_const_bitand(void);
static long long parse_enum_const_eq(void);
static long long parse_enum_const_rel(void);
static long long parse_enum_const_shift(void);
static long long parse_enum_const_add(void);
static long long parse_enum_const_mul(void);
static long long parse_enum_const_unary(void);
static long long parse_enum_const_primary(void);
static int parse_array_size_constexpr_stmt(void);
static int parse_alignas_value_stmt(void);
static void make_anonymous_tag_name_stmt(char **out_name, int *out_len);
static node_t *stmt_internal(void);
static node_t *block_item(void);
static int is_decl_like_start_stmt(void);
static node_t *parse_decl_like_stmt(void);
static int anonymous_tag_seq_stmt = 0;

static bool is_decl_prefix_token_stmt(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
}

static void make_anonymous_tag_name_stmt(char **out_name, int *out_len) {
  int seq = anonymous_tag_seq_stmt++;
  int len = snprintf(NULL, 0, "__anon_tag_stmt_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__anon_tag_stmt_%d", seq);
  *out_name = name;
  *out_len = len;
}

static void skip_ptr_qualifiers_stmt(void) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    set_curtok(curtok()->next);
  }
}

static void skip_func_params_stmt(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  while (depth > 0) {
    if (curtok()->kind == TK_EOF) {
      diag_emit_tokf(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
    }
    if (curtok()->kind == TK_LPAREN) depth++;
    else if (curtok()->kind == TK_RPAREN) depth--;
    set_curtok(curtok()->next);
  }
}

static token_ident_t *parse_typedef_name_decl_recursive(int *is_ptr) {
  while (tk_consume('*')) {
    *is_ptr = 1;
    skip_ptr_qualifiers_stmt();
  }
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_typedef_name_decl_recursive(is_ptr);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  while (curtok()->kind == TK_LPAREN) {
    skip_func_params_stmt();
  }
  return name;
}

static token_ident_t *parse_typedef_name_decl(int *is_ptr) {
  token_ident_t *name = parse_typedef_name_decl_recursive(is_ptr);
  if (!name) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED));
  }
  return name;
}

static token_ident_t *parse_member_decl_name_recursive_stmt(int *is_ptr, int *out_has_func_suffix) {
  while (tk_consume('*')) {
    *is_ptr = 1;
    skip_ptr_qualifiers_stmt();
  }
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_member_decl_name_recursive_stmt(is_ptr, out_has_func_suffix);
    while (tk_consume('[')) {
      int depth = 1;
      while (depth > 0) {
        if (curtok()->kind == TK_EOF) {
          diag_emit_tokf(DIAG_ERR_PARSER_EXPECTED_TOKEN, curtok(), "%s",
                         diag_message_for(DIAG_ERR_PARSER_EXPECTED_TOKEN));
        }
        if (curtok()->kind == TK_LBRACKET) depth++;
        else if (curtok()->kind == TK_RBRACKET) depth--;
        set_curtok(curtok()->next);
      }
    }
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  while (curtok()->kind == TK_LPAREN) {
    if (out_has_func_suffix) *out_has_func_suffix = 1;
    skip_func_params_stmt();
  }
  return name;
}

static int parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  int member_count = 0;
  int current_off = 0;
  int union_size = 0;
  int agg_align = 1;
  int bf_storage_offset = -1;
  int bf_storage_type_size = 0;
  int bf_bits_used = 0;
  #define ALIGN_UP(v, a) (((v) + ((a) - 1)) / (a) * (a))
  while (!tk_consume('}')) {
    int elem_size = 8;
    int is_signed_type = 1;
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    int member_alignas = 0;
    // skip leading qualifiers (const, volatile, _Alignas)
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_ALIGNAS) {
      if (curtok()->kind == TK_ALIGNAS) {
        set_curtok(curtok()->next);
        int av = parse_alignas_value_stmt();
        if (av > member_alignas) member_alignas = av;
      } else {
        set_curtok(curtok()->next);
      }
    }
    if (psx_ctx_is_type_token(curtok()->kind)) {
      is_signed_type = (curtok()->kind != TK_UNSIGNED);
      psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
      set_curtok(curtok()->next);
      while (psx_ctx_is_type_token(curtok()->kind)) {
        if (curtok()->kind != TK_UNSIGNED && curtok()->kind != TK_SIGNED)
          psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
        set_curtok(curtok()->next);
      }
    } else if (psx_ctx_is_tag_keyword(curtok()->kind)) {
      member_tag_kind = curtok()->kind;
      set_curtok(curtok()->next);
      token_ident_t *nested_tag = tk_consume_ident();
      if (nested_tag) {
        member_tag_name = nested_tag->str;
        member_tag_len = nested_tag->len;
      } else if (curtok()->kind == TK_LBRACE) {
        make_anonymous_tag_name_stmt(&member_tag_name, &member_tag_len);
      } else {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
      }
      if (tk_consume('{')) {
        int nested_n = 0;
        int nested_sz = 0;
        nested_n = parse_tag_definition_body(member_tag_kind, member_tag_name, member_tag_len, &nested_sz);
        psx_ctx_define_tag_type_with_layout(member_tag_kind, member_tag_name, member_tag_len, nested_n, nested_sz);
      } else if (!psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        // ポインタメンバの場合は不完全型（自己参照等）を許可する
        if (curtok()->kind != TK_MUL) {
          psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), member_tag_name, member_tag_len);
        }
      }
      if (psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        elem_size = psx_ctx_get_tag_size(member_tag_kind, member_tag_name, member_tag_len);
      }
      if (elem_size <= 0 && curtok()->kind != TK_MUL) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
      }
    } else {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }

    for (;;) {
      int is_ptr = 0;
      int has_func_suffix = 0;
      token_ident_t *member = parse_member_decl_name_recursive_stmt(&is_ptr, &has_func_suffix);
      int has_member_name = member != NULL;
      if (!has_member_name && !(member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)
          && curtok()->kind != TK_COLON) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      }
      if (has_func_suffix && !is_ptr) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
      }

      int bit_width = 0;
      int bit_field_offset_in_storage = 0;
      if (curtok()->kind == TK_COLON) {
        set_curtok(curtok()->next);
        long long bw = parse_enum_const_expr();
        if (bw < 0) bw = 0;
        bit_width = (int)bw;
        int storage_size = is_ptr ? 8 : (elem_size > 0 ? elem_size : 4);
        if (storage_size > 4) storage_size = 4;
        int storage_bits = storage_size * 8;
        if (bit_width == 0) {
          bf_storage_offset = -1;
          bf_bits_used = 0;
          if (tag_kind != TK_UNION)
            current_off = ALIGN_UP(current_off, storage_size);
          if (!has_member_name) { if (!tk_consume(',')) break; continue; }
        }
        if (tag_kind != TK_UNION) {
          if (bf_storage_offset < 0
              || bf_storage_type_size != storage_size
              || bf_bits_used + bit_width > storage_bits) {
            current_off = ALIGN_UP(current_off, storage_size);
            bf_storage_offset = current_off;
            bf_storage_type_size = storage_size;
            bf_bits_used = 0;
            current_off += storage_size;
            if (storage_size > agg_align) agg_align = storage_size;
          }
          bit_field_offset_in_storage = bf_bits_used;
          bf_bits_used += bit_width;
        } else {
          bit_field_offset_in_storage = 0;
          bf_storage_offset = 0;
          bf_storage_type_size = storage_size;
          if (storage_size > union_size) union_size = storage_size;
          if (storage_size > agg_align) agg_align = storage_size;
        }
        if (has_member_name) {
          int storage_type_size = bf_storage_type_size > 0 ? bf_storage_type_size : 4;
          psx_ctx_add_tag_member_bf(tag_kind, tag_name, tag_len,
                                    member->str, member->len,
                                    tag_kind == TK_UNION ? 0 : bf_storage_offset,
                                    storage_type_size, 0, 0,
                                    TK_EOF, NULL, 0, 0,
                                    bit_width, bit_field_offset_in_storage, is_signed_type);
          member_count++;
        }
        if (!tk_consume(',')) break;
        continue;
      }

      bf_storage_offset = -1;
      bf_bits_used = 0;

      int arr_size = 1;
      int is_flex_array = 0;
      while (tk_consume('[')) {
        if (!has_member_name) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
        if (curtok()->kind == TK_RBRACKET) {
          // フレキシブル配列メンバー: int data[];
          is_flex_array = 1;
          arr_size = 0;
        } else {
          arr_size *= parse_array_size_constexpr_stmt();
        }
        tk_expect(']');
      }
      int total_size = is_flex_array ? 0 : (is_ptr ? 8 : elem_size * arr_size);
      int deref_size = is_ptr ? elem_size : 0;
      int member_align = is_ptr ? 8 : elem_size;
      if (member_align <= 0) member_align = 1;
      if (member_align > 8) member_align = 8;
      if (member_alignas > member_align) member_align = member_alignas;
      if (member_align > agg_align) agg_align = member_align;
      int off = 0;
      if (tag_kind == TK_UNION) {
        off = 0;
      } else {
        current_off = ALIGN_UP(current_off, member_align);
        off = current_off;
      }
      char *member_name = has_member_name ? member->str : "";
      int member_len = has_member_name ? member->len : 0;
      int member_array_len = (is_ptr || arr_size <= 1) ? 0 : arr_size;
      if (has_member_name || (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)) {
        psx_ctx_add_tag_member(tag_kind, tag_name, tag_len,
                               member_name, member_len, off, is_ptr ? 8 : elem_size, deref_size,
                               member_array_len,
                               member_tag_kind, member_tag_name, member_tag_len, is_ptr ? 1 : 0);
        member_count++;
      }
      if (tag_kind == TK_UNION) {
        if (total_size > union_size) union_size = total_size;
      } else {
        current_off += total_size;
      }
      if (!has_member_name && tk_consume(',')) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  *out_size = (tag_kind == TK_UNION) ? ALIGN_UP(union_size, agg_align) : ALIGN_UP(current_off, agg_align);
  #undef ALIGN_UP
  return member_count;
}

static long long parse_enum_const_expr(void) { return parse_enum_const_conditional(); }

static long long parse_enum_const_conditional(void) {
  long long cond = parse_enum_const_logor();
  if (!tk_consume('?')) return cond;
  long long then_v = parse_enum_const_expr();
  tk_expect(':');
  long long else_v = parse_enum_const_conditional();
  return cond ? then_v : else_v;
}

static long long parse_enum_const_logor(void) {
  long long v = parse_enum_const_logand();
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    long long r = parse_enum_const_logand();
    v = (v || r) ? 1 : 0;
  }
  return v;
}

static long long parse_enum_const_logand(void) {
  long long v = parse_enum_const_bitor();
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    long long r = parse_enum_const_bitor();
    v = (v && r) ? 1 : 0;
  }
  return v;
}

static long long parse_enum_const_bitor(void) {
  long long v = parse_enum_const_bitxor();
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    long long r = parse_enum_const_bitxor();
    v |= r;
  }
  return v;
}

static long long parse_enum_const_bitxor(void) {
  long long v = parse_enum_const_bitand();
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    long long r = parse_enum_const_bitand();
    v ^= r;
  }
  return v;
}

static long long parse_enum_const_bitand(void) {
  long long v = parse_enum_const_eq();
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    long long r = parse_enum_const_eq();
    v &= r;
  }
  return v;
}

static long long parse_enum_const_eq(void) {
  long long v = parse_enum_const_rel();
  while (curtok()->kind == TK_EQEQ || curtok()->kind == TK_NEQ) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_enum_const_rel();
    v = (op == TK_EQEQ) ? (v == r) : (v != r);
  }
  return v;
}

static long long parse_enum_const_rel(void) {
  long long v = parse_enum_const_shift();
  while (curtok()->kind == TK_LT || curtok()->kind == TK_LE || curtok()->kind == TK_GT || curtok()->kind == TK_GE) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_enum_const_shift();
    switch (op) {
      case TK_LT: v = (v < r); break;
      case TK_LE: v = (v <= r); break;
      case TK_GT: v = (v > r); break;
      default: v = (v >= r); break;
    }
  }
  return v;
}

static long long parse_enum_const_shift(void) {
  long long v = parse_enum_const_add();
  while (curtok()->kind == TK_SHL || curtok()->kind == TK_SHR) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_enum_const_add();
    v = (op == TK_SHL) ? (v << r) : (v >> r);
  }
  return v;
}

static long long parse_enum_const_add(void) {
  long long v = parse_enum_const_mul();
  while (curtok()->kind == TK_PLUS || curtok()->kind == TK_MINUS) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_enum_const_mul();
    v = (op == TK_PLUS) ? (v + r) : (v - r);
  }
  return v;
}

static long long parse_enum_const_mul(void) {
  long long v = parse_enum_const_unary();
  while (curtok()->kind == TK_MUL || curtok()->kind == TK_DIV || curtok()->kind == TK_MOD) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_enum_const_unary();
    if (op == TK_MUL) v *= r;
    else if (op == TK_DIV) v /= r;
    else v %= r;
  }
  return v;
}

static long long parse_enum_const_unary(void) {
  if (curtok()->kind == TK_PLUS) {
    set_curtok(curtok()->next);
    return parse_enum_const_unary();
  }
  if (curtok()->kind == TK_MINUS) {
    set_curtok(curtok()->next);
    return -parse_enum_const_unary();
  }
  if (curtok()->kind == TK_TILDE) {
    set_curtok(curtok()->next);
    return ~parse_enum_const_unary();
  }
  if (curtok()->kind == TK_BANG) {
    set_curtok(curtok()->next);
    return !parse_enum_const_unary();
  }
  if (curtok()->kind == TK_SIZEOF) {
    set_curtok(curtok()->next);
    if (curtok()->kind == TK_LPAREN) {
      set_curtok(curtok()->next);
      int sz = 8;
      if (psx_ctx_is_type_token(curtok()->kind) || psx_ctx_is_tag_keyword(curtok()->kind) ||
          psx_ctx_is_typedef_name_token(curtok())) {
        psx_ctx_get_type_info(curtok()->kind, NULL, &sz);
        if (psx_ctx_is_tag_keyword(curtok()->kind)) {
          token_kind_t tk = curtok()->kind;
          set_curtok(curtok()->next);
          token_ident_t *tag = tk_consume_ident();
          if (tag && psx_ctx_has_tag_type(tk, tag->str, tag->len)) {
            sz = psx_ctx_get_tag_size(tk, tag->str, tag->len);
          }
        } else if (psx_ctx_is_typedef_name_token(curtok())) {
          token_ident_t *id = (token_ident_t *)curtok();
          token_kind_t td_base = TK_EOF;
          int td_elem = 8;
          tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
          token_kind_t td_tag = TK_EOF;
          char *td_tag_name = NULL;
          int td_tag_len = 0;
          int td_ptr = 0;
          int td_sizeof = 8;
          psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp,
                                    &td_tag, &td_tag_name, &td_tag_len, &td_ptr, NULL, NULL, NULL);
          if (psx_ctx_find_typedef_sizeof(id->str, id->len, &td_sizeof)) sz = td_sizeof;
          else sz = td_ptr ? 8 : td_elem;
          set_curtok(curtok()->next);
        } else {
          set_curtok(curtok()->next);
          // consume additional type specifiers (e.g., "unsigned long long")
          while (psx_ctx_is_type_token(curtok()->kind)) set_curtok(curtok()->next);
        }
        // skip pointer stars
        while (curtok()->kind == TK_MUL) { sz = 8; set_curtok(curtok()->next); }
      }
      tk_expect(')');
      return sz;
    }
    return parse_enum_const_unary();
  }
  return parse_enum_const_primary();
}

static long long parse_enum_const_primary(void) {
  if (curtok()->kind == TK_LPAREN) {
    set_curtok(curtok()->next);
    long long v = parse_enum_const_expr();
    tk_expect(')');
    return v;
  }
  token_ident_t *id = tk_consume_ident();
  if (id) {
    long long v = 0;
    if (!psx_ctx_find_enum_const(id->str, id->len, &v)) {
      psx_diag_ctx(curtok(), "enum", diag_message_for(DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED),
                   id->len, id->str);
    }
    return v;
  }
  return tk_expect_number();
}

static int parse_array_size_constexpr_stmt(void) {
  long long v = parse_enum_const_expr();
  if (v <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }
  return (int)v;
}

// _Alignas( constant-expression | type-name )
static int parse_alignas_value_stmt(void) {
  tk_expect('(');
  int val = 1;
  if (psx_ctx_is_type_token(curtok()->kind) || psx_ctx_is_typedef_name_token(curtok())) {
    int elem_size = 8;
    psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
    val = elem_size;
    while (curtok()->kind != TK_RPAREN && curtok()->kind != TK_EOF) set_curtok(curtok()->next);
  } else {
    long long v = parse_enum_const_expr();
    val = (v > 0) ? (int)v : 1;
  }
  tk_expect(')');
  return val;
}

static int parse_enum_members(void) {
  int member_count = 0;
  long long next_value = 0;
  while (!tk_consume('}')) {
    token_ident_t *enumerator = tk_consume_ident();
    if (!enumerator) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_ENUMERATOR_NAME));
    }
    long long value = next_value;
    if (tk_consume('=')) {
      value = parse_enum_const_expr();
    }
    psx_ctx_define_enum_const(enumerator->str, enumerator->len, value);
    next_value = value + 1;
    member_count++;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  return member_count;
}

static int parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    return parse_enum_members();
  }
  return parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size);
}

static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind) {
  *elem_size = 8;
  *fp_kind = TK_FLOAT_KIND_NONE;
  *tag_kind = TK_EOF;
  *tag_name = NULL;
  *tag_len = 0;
  *is_pointer_base = 0;
  *base_kind = TK_EOF;

  token_kind_t builtin_kind = psx_consume_type_kind();
  if (builtin_kind != TK_EOF) {
    *base_kind = builtin_kind;
    psx_ctx_get_type_info(builtin_kind, NULL, elem_size);
    if (builtin_kind == TK_FLOAT) *fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (builtin_kind == TK_DOUBLE) *fp_kind = TK_FLOAT_KIND_DOUBLE;
    return 1;
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    *base_kind = curtok()->kind;
    *tag_kind = curtok()->kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    static int anon_typedef_tag_counter = 0;
    char anon_buf[32];
    if (!tag && curtok()->kind != TK_LBRACE) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    *tag_name = tag ? tag->str : anon_buf;
    *tag_len = tag ? tag->len : 0;
    if (!tag) {
      *tag_len = snprintf(anon_buf, sizeof(anon_buf), "__anon_typedef_%d", anon_typedef_tag_counter++);
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body(*tag_kind, *tag_name, *tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(*tag_kind, *tag_name, *tag_len, member_count, tag_size);
    } else if (!psx_ctx_has_tag_type(*tag_kind, *tag_name, *tag_len)) {
      if (*tag_kind == TK_STRUCT || *tag_kind == TK_UNION) {
        psx_ctx_define_tag_type(*tag_kind, *tag_name, *tag_len);
      } else {
        psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), *tag_name, *tag_len);
      }
    }
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return 1;
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    token_ident_t *id = (token_ident_t *)curtok();
    if (!psx_ctx_find_typedef_name(id->str, id->len, base_kind, elem_size, fp_kind,
                                   tag_kind, tag_name, tag_len, is_pointer_base, NULL, NULL, NULL)) {
      return 0;
    }
    set_curtok(curtok()->next);
    return 1;
  }
  return 0;
}

static void parse_typedef_decl(void) {
  if (curtok()->kind != TK_TYPEDEF) {
    psx_diag_ctx(curtok(), "typedef", "%s",
                 diag_message_for(DIAG_ERR_PARSER_TYPEDEF_KEYWORD_REQUIRED));
  }
  set_curtok(curtok()->next);
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_pointer_base = 0;
  token_kind_t base_kind = TK_EOF;
  if (!parse_decl_type_spec(&elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_pointer_base, &base_kind)) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }
  int td_pointee_const = 0;
  int td_pointee_volatile = 0;
  psx_take_type_qualifiers(&td_pointee_const, &td_pointee_volatile);
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();

  for (;;) {
    int is_ptr = is_pointer_base;
    while (tk_consume('*')) {
      is_ptr = 1;
      skip_ptr_qualifiers_stmt();
    }
    token_ident_t *name = parse_typedef_name_decl(&is_ptr);
    int typedef_sizeof = is_ptr ? 8 : elem_size;
    while (tk_consume('[')) {
      int n = parse_array_size_constexpr_stmt();
      if (!is_ptr && n > 0) typedef_sizeof *= n;
      tk_expect(']');
    }
    token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
    psx_ctx_define_typedef_name(name->str, name->len, stored_base_kind, elem_size, fp_kind,
                                tag_kind, tag_name, tag_len, is_ptr, typedef_sizeof,
                                td_pointee_const, td_pointee_volatile, td_is_unsigned);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static int is_decl_like_start_stmt(void) {
  if (curtok()->kind == TK_TYPEDEF) return 1;
  if (curtok()->kind == TK_STATIC_ASSERT) return 1;
  if (psx_ctx_is_type_token(curtok()->kind) || is_decl_prefix_token_stmt(curtok()->kind) ||
      psx_ctx_is_typedef_name_token(curtok())) return 1;
  if (psx_ctx_is_tag_keyword(curtok()->kind)) return 1;
  return 0;
}

static node_t *parse_decl_like_stmt(void) {
  if (curtok()->kind == TK_TYPEDEF) {
    parse_typedef_decl();
    return psx_node_new_num(0);
  }

  if (curtok()->kind == TK_STATIC_ASSERT ||
      psx_ctx_is_type_token(curtok()->kind) || is_decl_prefix_token_stmt(curtok()->kind) ||
      psx_ctx_is_typedef_name_token(curtok())) {
    return psx_decl_parse_declaration();
  }

  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    token_kind_t tag_kind = curtok()->kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    // 匿名タグ（enum { A=1 }; など）: タグ名なしで '{' が来る場合
    if (!tag && curtok()->kind != TK_LBRACE) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    static int anon_tag_counter = 0;
    char anon_buf[32];
    char *tag_name = tag ? tag->str : anon_buf;
    int tag_len = tag ? tag->len : 0;
    if (!tag) {
      tag_len = snprintf(anon_buf, sizeof(anon_buf), "__anon_%d", anon_tag_counter++);
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body(tag_kind, tag_name, tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
      if (tk_consume(';')) {
        return psx_node_new_num(0);
      }
      return psx_decl_parse_declaration_after_type(tag_size, TK_FLOAT_KIND_NONE, tag_kind, tag_name, tag_len, 0, 0, 0, 0);
    }
    if (tk_consume(';')) {
      psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
      return psx_node_new_num(0);
    }
    if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag_name, tag_len);
    }
    int tag_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    return psx_decl_parse_declaration_after_type(tag_size > 0 ? tag_size : 8,
                                                 TK_FLOAT_KIND_NONE, tag_kind, tag_name, tag_len, 0, 0, 0, 0);
  }

  return NULL;
}

static node_t *block_item(void) {
  if (is_decl_like_start_stmt()) {
    return parse_decl_like_stmt();
  }

  return stmt_internal();
}

static node_t *stmt_internal(void) {
  // 空文（null statement）: C11 6.8.3 — セミコロンだけの文
  if (tk_consume(';')) {
    return psx_node_new_num(0);
  }

  if (tk_consume('{')) {
    psx_ctx_enter_block_scope();
    psx_decl_enter_scope();
    node_block_t *node = arena_alloc(sizeof(node_block_t));
    node->base.kind = ND_BLOCK;
    int i = 0;
    int cap = 16;
    node->body = calloc(cap, sizeof(node_t*));
    int prev_terminates = 0;
    while (!tk_consume('}')) {
      if (prev_terminates && curtok()->kind != TK_CASE && curtok()->kind != TK_DEFAULT &&
          !(curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COLON)) {
        diag_warn_tokf(DIAG_WARN_PARSER_UNREACHABLE_CODE, curtok(),
                       "%s", diag_warn_message_for(DIAG_WARN_PARSER_UNREACHABLE_CODE));
        prev_terminates = 0;
      }
      if (i >= cap - 1) {
        cap = pda_next_cap(cap, i + 2);
        node->body = pda_xreallocarray(node->body, (size_t)cap, sizeof(node_t *));
      }
      node->body[i] = block_item();
      node_kind_t k = node->body[i]->kind;
      prev_terminates = (k == ND_RETURN || k == ND_BREAK || k == ND_CONTINUE || k == ND_GOTO);
      i++;
    }
    node->body[i] = NULL;
    psx_decl_leave_scope();
    psx_ctx_leave_block_scope();
    return (node_t *)node;
  }

  if (is_decl_like_start_stmt()) {
    return parse_decl_like_stmt();
  }

  if (curtok()->kind == TK_RETURN) {
    set_curtok(curtok()->next);
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_RETURN;
    if (tk_consume(';')) {
      if (psx_expr_current_func_ret_token_kind() != TK_VOID) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                       "%s",
                       diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID));
      }
      node->lhs = NULL;
      node->fp_kind = psx_expr_current_func_ret_fp_kind();
      return node;
    }
    node->lhs = ps_expr();
    if (psx_expr_current_func_ret_token_kind() == TK_VOID) {
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                     "%s",
                     diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID));
    }
    node->fp_kind = psx_expr_current_func_ret_fp_kind();
    node->ret_struct_size = psx_expr_current_func_ret_struct_size();
    tk_expect(';');
    return node;
  }

  if (curtok()->kind == TK_IF) {
    set_curtok(curtok()->next);
    tk_expect('(');
    node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
    node->base.kind = ND_IF;
    node->base.lhs = ps_expr();
    tk_expect(')');
    node->base.rhs = stmt_internal();
    if (curtok()->kind == TK_ELSE) {
      set_curtok(curtok()->next);
      node->els = stmt_internal();
    }
    return (node_t *)node;
  }

  if (curtok()->kind == TK_WHILE) {
    set_curtok(curtok()->next);
    tk_expect('(');
    node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
    node->base.kind = ND_WHILE;
    node->base.lhs = ps_expr();
    tk_expect(')');
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_DO) {
    set_curtok(curtok()->next);
    node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
    node->base.kind = ND_DO_WHILE;
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    if (curtok()->kind != TK_WHILE) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_WHILE));
    }
    set_curtok(curtok()->next);
    tk_expect('(');
    node->base.lhs = ps_expr();
    tk_expect(')');
    tk_expect(';');
    return (node_t *)node;
  }

  if (curtok()->kind == TK_FOR) {
    set_curtok(curtok()->next);
    tk_expect('(');
    node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
    node->base.kind = ND_FOR;
    int for_has_decl = 0;
    if (!tk_consume(';')) {
      if (is_decl_like_start_stmt()) {
        for_has_decl = 1;
        psx_decl_enter_scope();
        node->init = parse_decl_like_stmt();
      } else {
        node->init = ps_expr();
        tk_expect(';');
      }
    }
    if (!tk_consume(';')) {
      node->base.lhs = ps_expr();
      tk_expect(';');
    }
    if (!tk_consume(')')) {
      node->inc = ps_expr();
      tk_expect(')');
    }
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    if (for_has_decl) psx_decl_leave_scope();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_SWITCH) {
    set_curtok(curtok()->next);
    tk_expect('(');
    node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
    node->base.kind = ND_SWITCH;
    node->base.lhs = ps_expr();
    tk_expect(')');
    psx_switch_push_ctx();
    node->base.rhs = stmt_internal();
    psx_switch_pop_ctx();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_CASE) {
    set_curtok(curtok()->next);
    node_case_t *node = arena_alloc(sizeof(node_case_t));
    node->base.kind = ND_CASE;
    node->val = parse_enum_const_expr();
    psx_switch_register_case(node->val, curtok());
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_DEFAULT) {
    set_curtok(curtok()->next);
    psx_switch_register_default(curtok());
    node_default_t *node = arena_alloc(sizeof(node_default_t));
    node->base.kind = ND_DEFAULT;
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_BREAK) {
    if (psx_loop_depth() == 0 && !psx_switch_has_ctx()) {
      psx_diag_only_in(curtok(), diag_text_for(DIAG_TEXT_BREAK), diag_text_for(DIAG_TEXT_LOOP_OR_SWITCH_SCOPE));
    }
    set_curtok(curtok()->next);
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_BREAK;
    tk_expect(';');
    return node;
  }

  if (curtok()->kind == TK_CONTINUE) {
    if (psx_loop_depth() == 0) {
      psx_diag_only_in(curtok(), diag_text_for(DIAG_TEXT_CONTINUE), diag_text_for(DIAG_TEXT_LOOP_SCOPE));
    }
    set_curtok(curtok()->next);
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_CONTINUE;
    tk_expect(';');
    return node;
  }

  if (curtok()->kind == TK_GOTO) {
    token_t *goto_tok = curtok();
    set_curtok(curtok()->next);
    token_ident_t *ident = tk_consume_ident();
    if (!ident) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_GOTO_LABEL_AFTER));
    }
    node_jump_t *node = arena_alloc(sizeof(node_jump_t));
    node->base.kind = ND_GOTO;
    node->name = ident->str;
    node->name_len = ident->len;
    psx_ctx_register_goto_ref(ident->str, ident->len, goto_tok);
    tk_expect(';');
    return (node_t *)node;
  }

  if (curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COLON) {
    token_ident_t *ident = tk_consume_ident();
    tk_expect(':');
    node_jump_t *node = arena_alloc(sizeof(node_jump_t));
    node->base.kind = ND_LABEL;
    node->name = ident->str;
    node->name_len = ident->len;
    psx_ctx_register_label_def(ident->str, ident->len, curtok());
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  node_t *node = ps_expr();
  tk_expect(';');
  return node;
}

node_t *psx_stmt_stmt(void) {
  return stmt_internal();
}
