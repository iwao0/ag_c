#include "parser.h"
#include "parser_recovery.h"
#include "literal_public.h"
#include "arena.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "static_assert_declaration.h"
#include "decl.h"
#include "core.h"
#include "alignas_value.h"
#include "array_suffixes.h"
#include "diag.h"
#include "declarator_syntax.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "global_registry.h"
#include "local_registry.h"
#include "lvar_internal.h"
#include "stmt.h"
#include "type.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_recoverable_syntax_error;
static int g_function_block_depth;
static int g_recovery_block_depth;

int ps_gvar_is_extern_decl(const global_var_t *gv) {
  return (gv && gv->is_extern_decl) ? 1 : 0;
}

int ps_gvar_is_thread_local(const global_var_t *gv) {
  return (gv && gv->is_thread_local) ? 1 : 0;
}

int ps_gvar_is_static_storage(const global_var_t *gv) {
  return (gv && gv->is_static) ? 1 : 0;
}

int ps_gvar_is_extern_decl_by_name(char *name, int len) {
  return ps_gvar_is_extern_decl(ps_find_global_var(name, len));
}

int ps_gvar_is_thread_local_by_name(char *name, int len) {
  return ps_gvar_is_thread_local(ps_find_global_var(name, len));
}

int ps_gvar_is_static_storage_by_name(char *name, int len) {
  return ps_gvar_is_static_storage(ps_find_global_var(name, len));
}

char *ps_gvar_name(const global_var_t *gv) {
  return gv ? gv->name : NULL;
}

int ps_gvar_name_len(const global_var_t *gv) {
  return gv ? gv->name_len : 0;
}

psx_string_lit_view_t ps_string_lit_view(const string_lit_t *lit) {
  if (!lit) return (psx_string_lit_view_t){0};
  return (psx_string_lit_view_t){
      .label = lit->label,
      .str = lit->str,
      .len = lit->len,
      .char_width = lit->char_width,
  };
}

psx_float_lit_view_t ps_float_lit_view(const float_lit_t *lit) {
  if (!lit) return (psx_float_lit_view_t){.fp_kind = TK_FLOAT_KIND_NONE};
  return (psx_float_lit_view_t){
      .fval = lit->fval,
      .id = lit->id,
      .fp_kind = lit->fp_kind,
  };
}

static token_kind_t parse_atomic_type_specifier(void);
static void psx_type_spec_result_reset(psx_type_spec_result_t *out);
static void skip_cv_qualifiers_into_ex(
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax);
static inline token_t *curtok(void);
static inline void set_curtok(token_t *tok);

bool psx_is_decl_prefix_token(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
}

bool psx_is_gnu_attribute_token(const token_t *t) {
  if (!t || t->kind != TK_IDENT) return 0;
  const token_ident_t *id = (const token_ident_t *)t;
  return id->len == 13 && memcmp(id->str, "__attribute__", 13) == 0;
}

void psx_skip_gnu_attributes_at(token_t **t) {
  while (*t && psx_is_gnu_attribute_token(*t)) {
    *t = (*t)->next;
    if (!*t || (*t)->kind != TK_LPAREN) continue;
    int depth = 0;
    while (*t) {
      if ((*t)->kind == TK_LPAREN) depth++;
      else if ((*t)->kind == TK_RPAREN) {
        depth--;
        *t = (*t)->next;
        if (depth == 0) break;
        continue;
      }
      *t = (*t)->next;
    }
  }
}

void psx_skip_gnu_attributes(void) {
  while (psx_is_gnu_attribute_token(curtok())) {
    token_t *t = curtok();
    psx_skip_gnu_attributes_at(&t);
    set_curtok(t);
  }
}

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

static void skip_cv_qualifiers_into_ex(
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax) {
  psx_type_spec_result_reset(out);
  /* C11 6.7.1p2: 宣言指定子に storage class 指定子は高々 1 個。
   * 例外として _Thread_local は static / extern と一緒に書ける。 */
  int storage_count = 0;
  int saw_thread_local = 0;
  token_t *first_storage_tok = NULL;
  while (psx_is_decl_prefix_token(curtok()->kind)) {
    if (curtok()->kind == TK_CONST) out->is_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE) out->is_volatile_qualified = 1;
    if (curtok()->kind == TK_EXTERN) out->is_extern = 1;
    if (curtok()->kind == TK_STATIC) out->is_static = 1;
    if (curtok()->kind == TK_EXTERN || curtok()->kind == TK_STATIC ||
        curtok()->kind == TK_AUTO || curtok()->kind == TK_REGISTER) {
      if (!first_storage_tok) first_storage_tok = curtok();
      storage_count++;
    }
    if (curtok()->kind == TK_THREAD_LOCAL) saw_thread_local = 1;
    if (curtok()->kind == TK_ALIGNAS) {
      if (syntax && syntax->consume_alignas) {
        syntax->consume_alignas(syntax->context, out);
        continue;
      }
      set_curtok(curtok()->next);
      if (curtok()->kind != TK_LPAREN) {
        ps_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED));
      }
      int av = psx_parse_alignas_value();
      if (av > out->alignas_value) out->alignas_value = av;
      continue;
    }
    if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind == TK_LPAREN) {
      return;
    }
    if (curtok()->kind == TK_ATOMIC) {
      out->is_atomic = 1;
    }
    if (curtok()->kind == TK_THREAD_LOCAL) {
      out->is_thread_local = 1;
    }
    set_curtok(curtok()->next);
  }
  /* storage class が 2 個以上同時指定されているとエラー。
   * `_Thread_local` 単独は storage_count に数えていないので
   * `_Thread_local int x;` は 0 で通り、`static _Thread_local int x;` は 1 で通る。 */
  if (storage_count > 1) {
    ps_diag_ctx(first_storage_tok, "decl",
                 "storage class 指定子は1つまでです (C11 6.7.1p2)");
  }
  (void)saw_thread_local;
  psx_skip_gnu_attributes();
}

static token_kind_t parse_atomic_type_specifier(void) {
  if (curtok()->kind != TK_ATOMIC) return TK_EOF;
  set_curtok(curtok()->next);
  if (!tk_consume('(')) {
    // qualifier-form: "_Atomic int" は前置指定子として扱う
    return TK_EOF;
  }
  psx_type_spec_result_t inner_spec;
  token_kind_t inner = psx_consume_type_kind_ex(&inner_spec);
  if (inner == TK_EOF) {
    ps_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED));
  }
  // Minimal support for derived declarators in _Atomic(type), e.g. _Atomic(int*).
  while (tk_consume('*')) {
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
      set_curtok(curtok()->next);
    }
  }
  tk_expect(')');
  return inner;
}

// 現在のトークンが #pragma pack マーカーなら対応する関数を呼んで消費し true を返す。
// プリプロセッサはマーカーを出現位置に挿入するだけなので、トップレベルだけでなく
// 関数本体のブロック内でも遭遇しうる。透過的に処理する。
bool psx_try_consume_pragma_pack_marker(void) {
  token_kind_t k = curtok()->kind;
  if (k == TK_PRAGMA_PACK_PUSH) {
    pragma_pack_push((int)((token_num_int_t *)curtok())->val);
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_POP) {
    pragma_pack_pop();
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_SET) {
    pragma_pack_set((int)((token_num_int_t *)curtok())->val);
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_RESET) {
    pragma_pack_reset();
    set_curtok(curtok()->next);
    return true;
  }
  return false;
}

// program = funcdef*
void ps_parser_stream_begin(
    psx_parser_stream_t *stream,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations) {
  ps_parser_stream_begin_in_context(
      stream, ps_ctx_active(), tk_ctx, start, toplevel_declarations);
}

void ps_parser_stream_begin_in_context(
    psx_parser_stream_t *stream,
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations) {
  ps_parser_stream_begin_in_contexts(
      stream, semantic_context, ps_local_registry_active(),
      tk_ctx, start, toplevel_declarations);
}

void ps_parser_stream_begin_in_contexts(
    psx_parser_stream_t *stream,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations) {
  if (!stream || !semantic_context || !local_registry) abort();
  stream->tk_ctx = tk_ctx;
  stream->semantic_context = semantic_context;
  stream->local_registry = local_registry;
  stream->toplevel_declarations = toplevel_declarations;
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  g_recoverable_syntax_error = 0;
  g_function_block_depth = 0;
  g_recovery_block_depth = 0;
}

static void psx_advance_recovery_token(void) {
  tk_ensure_lookahead();
  token_t *token = curtok();
  if (token && token->next) set_curtok(token->next);
}

static void psx_synchronize_toplevel_declaration(void) {
  token_t *start = curtok();
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  while (!tk_at_eof()) {
    token_kind_t kind = curtok()->kind;
    if (kind == TK_LPAREN) paren_depth++;
    else if (kind == TK_RPAREN && paren_depth > 0) paren_depth--;
    else if (kind == TK_LBRACKET) bracket_depth++;
    else if (kind == TK_RBRACKET && bracket_depth > 0) bracket_depth--;
    else if (kind == TK_LBRACE) brace_depth++;
    else if (kind == TK_RBRACE && brace_depth > 0) brace_depth--;
    psx_advance_recovery_token();
    if (kind == TK_SEMI && paren_depth == 0 && bracket_depth == 0 &&
        brace_depth == 0)
      break;
    if (kind == TK_RBRACE && paren_depth == 0 && bracket_depth == 0 &&
        brace_depth == 0)
      break;
  }
  if (curtok() == start && !tk_at_eof()) psx_advance_recovery_token();
}

int ps_parse_next_toplevel_item(
    psx_parser_stream_t *stream, psx_parsed_toplevel_item_t *item) {
  if (!stream || !stream->semantic_context || !item) return 0;
  psx_semantic_context_t *semantic_context = stream->semantic_context;
  *item = (psx_parsed_toplevel_item_t){0};
  while (!tk_at_eof()) {
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (curtok()->kind == TK_STATIC_ASSERT) {
      item->kind = PSX_TOPLEVEL_ITEM_STATIC_ASSERT;
      psx_parse_static_assert_syntax_in_contexts(
          &item->value.static_assertion,
          semantic_context, stream->local_registry,
          NULL);
      if (stream && stream->tk_ctx) {
        tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
      }
      return 1;
    }
    psx_parsed_toplevel_declaration_t declaration = {0};
    if (!psx_parse_toplevel_declaration_head_syntax_in_context(
            &declaration, semantic_context)) {
      ps_dispose_toplevel_declaration_syntax(&declaration);
      psx_synchronize_toplevel_declaration();
      if (agc_wasm_diagnostic_limit_kind()) break;
      continue;
    }
    psx_skip_gnu_attributes();
    if (!declaration.is_standalone_tag && curtok()->kind == TK_LBRACE) {
      psx_parsed_declarator_t *declarator = &declaration.declarators[0];
      if (declaration.is_typedef ||
          declarator->function_suffix_count <= 0) {
        ps_diag_ctx(declarator->diagnostic_token, "funcdef", "%s",
                    diag_message_for(
                        DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
      }
      item->kind = PSX_TOPLEVEL_ITEM_FUNCTION_HEADER;
      psx_move_toplevel_declaration_head_to_function_definition(
          &declaration, &item->value.function_header);
    } else {
      item->kind = PSX_TOPLEVEL_ITEM_DECLARATION;
      item->value.declaration = declaration;
      if (!psx_finish_toplevel_declaration_syntax_in_context(
              &item->value.declaration,
              stream->toplevel_declarations, semantic_context)) {
        ps_dispose_toplevel_declaration_syntax(&item->value.declaration);
        item->kind = PSX_TOPLEVEL_ITEM_EOF;
        psx_synchronize_toplevel_declaration();
        if (agc_wasm_diagnostic_limit_kind()) break;
        continue;
      }
    }
    if (stream && stream->tk_ctx) {
      tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
    }
    return 1;
  }
  if (stream && stream->tk_ctx) {
    tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
  }
  item->kind = PSX_TOPLEVEL_ITEM_EOF;
  return 0;
}

void ps_parser_stream_end(psx_parser_stream_t *stream) {
  if (stream) {
    stream->tk_ctx = NULL;
    stream->semantic_context = NULL;
    stream->local_registry = NULL;
    stream->toplevel_declarations = NULL;
  }
}

static void psx_type_spec_result_reset(psx_type_spec_result_t *out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->kind = TK_EOF;
}

static void emit_invalid_type_spec_diag(void) {
  diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
/* 後置 cv/atomic 修飾子トークンを 1 つ消費する。const/volatile/restrict/atomic
 * いずれも同じ「対応 flag を立てて trailing トークンを進める」パターンなので
 * 集約する。消費したら 1、該当しなければ 0 (呼出側で loop を抜ける)。 */
static int try_consume_post_cv_qualifier(psx_type_spec_result_t *out, token_kind_t k) {
  switch (k) {
    case TK_CONST:    out->is_const_qualified = 1; break;
    case TK_VOLATILE: out->is_volatile_qualified = 1; break;
    case TK_RESTRICT: break;
    case TK_ATOMIC:   out->is_atomic = 1; break;
    default: return 0;
  }
  set_curtok(curtok()->next);
  return 1;
}

/* saw_* flag 群から最終的な型 token_kind_t を決定する。
 * 優先度: void > float > double > bool > char > short > long > int。 */
static token_kind_t resolve_type_kind_from_flags(int saw_void, int saw_float, int saw_double,
                                                  int saw_bool, int saw_char, int saw_short,
                                                  int long_count) {
  if (saw_void) return TK_VOID;
  if (saw_float) return TK_FLOAT;
  if (saw_double) return TK_DOUBLE;
  if (saw_bool) return TK_BOOL;
  if (saw_char) return TK_CHAR;
  if (saw_short) return TK_SHORT;
  if (long_count > 0) return TK_LONG;
  return TK_INT;
}

token_kind_t psx_consume_type_kind_ex(psx_type_spec_result_t *out) {
  return psx_consume_type_kind_with_syntax_ex(out, NULL);
}

token_kind_t psx_consume_type_kind_with_syntax_ex(
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax) {
  psx_type_spec_result_t local;
  if (!out) out = &local;
  skip_cv_qualifiers_into_ex(out, syntax);
  if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind == TK_LPAREN) {
    out->is_atomic = 1;
    token_kind_t inner = parse_atomic_type_specifier();
    if (inner != TK_EOF) {
      out->kind = inner;
      return inner;
    }
  }
  // qualifier-form: _Atomic int x;
  if (curtok()->kind == TK_ATOMIC) {
    out->is_atomic = 1;
    set_curtok(curtok()->next);
  }
  token_t *start = curtok();
  int saw_signed = 0;
  int saw_unsigned = 0;
  int long_count = 0;
  int saw_short = 0;
  int saw_int = 0;
  int saw_char = 0;
  int saw_void = 0;
  int saw_float = 0;
  int saw_double = 0;
  int saw_bool = 0;
  int saw_complex = 0;
  int saw_imaginary = 0;

  while (true) {
    token_kind_t k = curtok()->kind;
    if (k == TK_COMPLEX) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_complex = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_IMAGINARY) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_imaginary = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_SIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_signed = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_UNSIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_unsigned = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_LONG) {
      if (saw_char || saw_short || saw_void || saw_float || saw_bool || long_count >= 2) {
        emit_invalid_type_spec_diag();
      }
      long_count++;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_SHORT) {
      if (saw_char || saw_short || long_count || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_short = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_INT) {
      if (saw_int || saw_char || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_int = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_CHAR) {
      if (saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_char = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_VOID) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_void = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_FLOAT) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_float = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_DOUBLE) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || saw_int || saw_void || saw_float || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_double = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_BOOL) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double) {
        emit_invalid_type_spec_diag();
      }
      saw_bool = 1;
      set_curtok(curtok()->next);
      continue;
    }
    // 後置 cv 修飾子（int const, volatile int const など）は同じ形なので集約。
    if (try_consume_post_cv_qualifier(out, k)) continue;
    /* C11 6.7p1: declaration-specifiers の順序は任意。型指定子の後ろに storage class
     * (static / extern / auto / register / inline / _Noreturn / _Thread_local / _Alignas) が
     * 来てもよい (`int static x = 5;` 等)。ここで遭遇したら skip_cv_qualifiers と同じ要領で
     * 1 つ消費して flag を立てループ継続。skip_cv_qualifiers を直接呼ぶと先頭で reset され
     * 既に立っている qualifier 情報 (const/volatile/atomic) を失うため、ここでは OR 的に
     * 1 トークンずつ処理する。 */
    if (psx_is_decl_prefix_token(k)) {
      /* storage class の重複・併用検出 (C11 6.7.1p2): static / extern / auto / register は
       * 高々 1 個。型指定子の前 (skip_cv_qualifiers) ですでに 1 つ立っていたら 2 つ目で error。 */
      int is_new_storage = (k == TK_STATIC || k == TK_EXTERN ||
                            k == TK_AUTO || k == TK_REGISTER);
      if (is_new_storage && (out->is_static || out->is_extern)) {
        ps_diag_ctx(curtok(), "decl",
                     "storage class 指定子は1つまでです (C11 6.7.1p2)");
      }
      if (k == TK_CONST)        out->is_const_qualified = 1;
      else if (k == TK_VOLATILE) out->is_volatile_qualified = 1;
      else if (k == TK_STATIC)   out->is_static = 1;
      else if (k == TK_EXTERN)   out->is_extern = 1;
      else if (k == TK_THREAD_LOCAL) out->is_thread_local = 1;
      else if (k == TK_ATOMIC) {
        /* `int _Atomic(int) x` 形式は ATOMIC 後に `(` が来る (型指定子)。型指定子の後の
         * 単独 `_Atomic` は qualifier 形 (`int _Atomic x`)。 */
        if (curtok()->next && curtok()->next->kind == TK_LPAREN) break;
        out->is_atomic = 1;
      }
      /* TK_AUTO / TK_REGISTER / TK_INLINE / TK_NORETURN / TK_ALIGNAS(...) は flag を立てずに
       * 単純消費。TK_ALIGNAS は `(value)` 形のため複雑だが、型指定子の後の出現は稀 (実例は
       * `int _Alignas(8) x` で C11 では基本的に typespec の前)。ここでは省略 — 必要ならば
       * 既存の skip_cv_qualifiers の TK_ALIGNAS 分岐を引用する。 */
      set_curtok(curtok()->next);
      continue;
    }
    break;
  }

  int saw_type_specifier =
      saw_signed || saw_unsigned || long_count > 0 || saw_short || saw_int ||
      saw_char || saw_void || saw_float || saw_double || saw_bool ||
      saw_complex || saw_imaginary;
  if (!saw_type_specifier) return TK_EOF;
  out->is_unsigned = saw_unsigned;
  out->is_complex = saw_complex || saw_imaginary;
  out->is_long_long = (long_count >= 2) ? 1 : 0;
  out->is_plain_char = (saw_char && !saw_signed && !saw_unsigned) ? 1 : 0;
  out->is_long_double = (saw_double && long_count >= 1) ? 1 : 0;
  if ((saw_complex || saw_imaginary) && !(saw_float || saw_double)) {
    if (syntax && syntax->diagnose_complex_requires_float) {
      syntax->diagnose_complex_requires_float(syntax->context, start);
    } else {
      diag_emit_tokf(
          DIAG_ERR_PARSER_INVALID_CONTEXT, start, "%s",
          diag_message_for(
              DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT));
    }
  }
  out->kind = resolve_type_kind_from_flags(saw_void, saw_float, saw_double, saw_bool,
                                           saw_char, saw_short, long_count);
  return out->kind;
}


// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
/* 関数本体の `{ ... }` を 1 つの node_block_t にパースする。
 * 既に opening `{` は呼出側が consume 済みの前提。block scope を enter / leave し、
 * 後段 semantic pass 用に各 statement の診断 token / usage region を保持する。
 * pragma pack マーカーは透過に消費する。 */
static node_block_t *parse_funcdef_body_block(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations) {
  ps_ctx_enter_block_scope_in(semantic_context);
  ps_parser_enter_recovery_block();
  node_block_t *body = arena_alloc(sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t *));
  while (!tk_consume('}')) {
    // #pragma pack マーカーは関数本体冒頭・任意の位置で出現しうる。透過処理。
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap(body_cap, i + 2);
      body->body = pda_xreallocarray(body->body, (size_t)body_cap, sizeof(node_t *));
    }
    token_t *stmt_tok = curtok();
    psx_lvar_usage_region_t *region =
        psx_decl_begin_lvar_usage_region_in(local_registry);
    body->body[i] = psx_stmt_stmt_in_contexts(
        semantic_context, local_registry, local_declarations);
    psx_decl_end_lvar_usage_region_in(local_registry, region);
    if (ps_parser_has_recoverable_syntax_error()) {
      body->body[i] = NULL;
      ps_parser_leave_recovery_block();
      ps_ctx_leave_block_scope_in(semantic_context);
      return NULL;
    }
    if (body->body[i]) {
      body->body[i]->tok = stmt_tok;
      body->body[i]->usage_region = region;
    }
    i++;
  }
  body->body[i] = NULL;
  ps_parser_leave_recovery_block();
  ps_ctx_leave_block_scope_in(semantic_context);
  return body;
}

node_t *ps_parse_function_definition_body(
    psx_parser_stream_t *stream, node_function_definition_t *function,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!stream || !stream->semantic_context ||
      !stream->local_registry || !function) return NULL;
  psx_semantic_context_t *semantic_context = stream->semantic_context;
  g_recoverable_syntax_error = 0;
  g_recovery_block_depth = 0;
  tk_expect('{');
  function->base.rhs =
      (node_t *)parse_funcdef_body_block(
          semantic_context, stream->local_registry, local_declarations);
  if (g_recoverable_syntax_error) {
    int depth = g_recovery_block_depth > 0 ? g_recovery_block_depth : 1;
    while (!tk_at_eof() && depth > 0) {
      token_kind_t kind = curtok()->kind;
      if (kind == TK_LBRACE) depth++;
      else if (kind == TK_RBRACE) depth--;
      psx_advance_recovery_token();
    }
    ps_decl_set_current_funcname(NULL, 0);
    if (stream && stream->tk_ctx)
      tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
    g_recoverable_syntax_error = 0;
    g_recovery_block_depth = 0;
    return NULL;
  }
  psx_ctx_validate_goto_refs_in(semantic_context);
  function->lvars = ps_decl_get_locals_in(stream->local_registry);
  ps_decl_set_current_funcname(NULL, 0);
  if (stream && stream->tk_ctx) {
    tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
  }
  return (node_t *)function;
}

// expr = assign ("," assign)*
node_t *ps_expr_ctx(tokenizer_context_t *tk_ctx, token_t *start) {
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  node_t *node = psx_expr_expr();
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, tk_get_current_token());
  }
  return node;
}

node_t *ps_expr_from(token_t *start) {
  return ps_expr_ctx(NULL, start);
}

node_t *ps_expr(void) {
  return ps_expr_ctx(NULL, tk_get_current_token());
}
void ps_parser_mark_recoverable_syntax_error(void) {
  if (!g_recoverable_syntax_error)
    g_recovery_block_depth = g_function_block_depth;
  g_recoverable_syntax_error = 1;
}

int ps_parser_has_recoverable_syntax_error(void) {
  return g_recoverable_syntax_error;
}

void ps_parser_enter_recovery_block(void) { g_function_block_depth++; }

void ps_parser_leave_recovery_block(void) {
  if (g_function_block_depth > 0) g_function_block_depth--;
}
