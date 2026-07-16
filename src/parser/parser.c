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
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ps_gvar_is_extern_decl(const global_var_t *gv) {
  return (gv && gv->is_extern_decl) ? 1 : 0;
}

int ps_gvar_is_thread_local(const global_var_t *gv) {
  return (gv && gv->is_thread_local) ? 1 : 0;
}

int ps_gvar_is_static_storage(const global_var_t *gv) {
  return (gv && gv->is_static) ? 1 : 0;
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

static token_kind_t parse_atomic_type_specifier(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context);
static void psx_type_spec_result_reset(psx_type_spec_result_t *out);
static void skip_cv_qualifiers_into_ex(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context,
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax);
static inline token_t *curtok_in(tokenizer_context_t *tokenizer_context);
static inline void set_curtok_in(
    tokenizer_context_t *tokenizer_context, token_t *tok);

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

void psx_skip_gnu_attributes_ctx(
    tokenizer_context_t *tokenizer_context) {
  while (psx_is_gnu_attribute_token(curtok_in(tokenizer_context))) {
    token_t *t = curtok_in(tokenizer_context);
    psx_skip_gnu_attributes_at(&t);
    set_curtok_in(tokenizer_context, t);
  }
}

static inline token_t *curtok_in(
    tokenizer_context_t *tokenizer_context) {
  return tk_get_current_token_ctx(tokenizer_context);
}

static inline void set_curtok_in(
    tokenizer_context_t *tokenizer_context, token_t *tok) {
  tk_set_current_token_ctx(tokenizer_context, tok);
}

static inline ag_diagnostic_context_t *diagnostics(
    psx_semantic_context_t *semantic_context) {
  return ps_ctx_diagnostics(semantic_context);
}

static void skip_cv_qualifiers_into_ex(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context,
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax) {
  psx_type_spec_result_reset(out);
  /* C11 6.7.1p2: 宣言指定子に storage class 指定子は高々 1 個。
   * 例外として _Thread_local は static / extern と一緒に書ける。 */
  int storage_count = 0;
  int saw_thread_local = 0;
  token_t *first_storage_tok = NULL;
  while (psx_is_decl_prefix_token(curtok_in(tokenizer_context)->kind)) {
    if (curtok_in(tokenizer_context)->kind == TK_CONST)
      out->is_const_qualified = 1;
    if (curtok_in(tokenizer_context)->kind == TK_VOLATILE)
      out->is_volatile_qualified = 1;
    if (curtok_in(tokenizer_context)->kind == TK_EXTERN) out->is_extern = 1;
    if (curtok_in(tokenizer_context)->kind == TK_STATIC) out->is_static = 1;
    if (curtok_in(tokenizer_context)->kind == TK_EXTERN ||
        curtok_in(tokenizer_context)->kind == TK_STATIC ||
        curtok_in(tokenizer_context)->kind == TK_AUTO ||
        curtok_in(tokenizer_context)->kind == TK_REGISTER) {
      if (!first_storage_tok)
        first_storage_tok = curtok_in(tokenizer_context);
      storage_count++;
    }
    if (curtok_in(tokenizer_context)->kind == TK_THREAD_LOCAL)
      saw_thread_local = 1;
    if (curtok_in(tokenizer_context)->kind == TK_ALIGNAS) {
      if (syntax && syntax->consume_alignas) {
        syntax->consume_alignas(
            syntax->consume_alignas_context, out);
        continue;
      }
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      if (curtok_in(tokenizer_context)->kind != TK_LPAREN) {
        ps_diag_ctx_in(
            diagnostics(semantic_context), curtok_in(tokenizer_context),
            "decl", "%s",
            diag_message_for_in(
                diagnostics(semantic_context),
                DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED));
      }
      int av = psx_parse_alignas_value_in_contexts(
          semantic_context, syntax ? syntax->name_classifier : NULL,
          tokenizer_context);
      if (av > out->alignas_value) out->alignas_value = av;
      continue;
    }
    if (curtok_in(tokenizer_context)->kind == TK_ATOMIC &&
        curtok_in(tokenizer_context)->next &&
        curtok_in(tokenizer_context)->next->kind == TK_LPAREN) {
      return;
    }
    if (curtok_in(tokenizer_context)->kind == TK_ATOMIC) {
      out->is_atomic = 1;
    }
    if (curtok_in(tokenizer_context)->kind == TK_THREAD_LOCAL) {
      out->is_thread_local = 1;
    }
    set_curtok_in(
        tokenizer_context, curtok_in(tokenizer_context)->next);
  }
  /* storage class が 2 個以上同時指定されているとエラー。
   * `_Thread_local` 単独は storage_count に数えていないので
   * `_Thread_local int x;` は 0 で通り、`static _Thread_local int x;` は 1 で通る。 */
  if (storage_count > 1) {
    ps_diag_ctx_in(
        diagnostics(semantic_context), first_storage_tok, "decl",
        "storage class 指定子は1つまでです (C11 6.7.1p2)");
  }
  (void)saw_thread_local;
  psx_skip_gnu_attributes_ctx(tokenizer_context);
}

static token_kind_t parse_atomic_type_specifier(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context) {
  if (curtok_in(tokenizer_context)->kind != TK_ATOMIC) return TK_EOF;
  set_curtok_in(
      tokenizer_context, curtok_in(tokenizer_context)->next);
  if (!tk_consume_ctx(tokenizer_context, '(')) {
    // qualifier-form: "_Atomic int" は前置指定子として扱う
    return TK_EOF;
  }
  psx_type_spec_result_t inner_spec;
  token_kind_t inner = psx_consume_type_kind_in_contexts(
      semantic_context, tokenizer_context, &inner_spec);
  if (inner == TK_EOF) {
    ps_diag_ctx_in(
        diagnostics(semantic_context), curtok_in(tokenizer_context),
        "decl", "%s",
        diag_message_for_in(
            diagnostics(semantic_context),
            DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED));
  }
  // Minimal support for derived declarators in _Atomic(type), e.g. _Atomic(int*).
  while (tk_consume_ctx(tokenizer_context, '*')) {
    while (curtok_in(tokenizer_context)->kind == TK_CONST ||
           curtok_in(tokenizer_context)->kind == TK_VOLATILE ||
           curtok_in(tokenizer_context)->kind == TK_RESTRICT) {
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
    }
  }
  tk_expect_ctx(tokenizer_context, ')');
  return inner;
}

// 現在のトークンが #pragma pack マーカーなら対応する関数を呼んで消費し true を返す。
// プリプロセッサはマーカーを出現位置に挿入するだけなので、トップレベルだけでなく
// 関数本体のブロック内でも遭遇しうる。透過的に処理する。
bool psx_try_consume_pragma_pack_marker_in(
    psx_parser_runtime_context_t *runtime_context) {
  if (!runtime_context) return false;
  tokenizer_context_t *tokenizer_context =
      ps_parser_runtime_tokenizer(runtime_context);
  if (!tokenizer_context) return false;
  token_t *token = tk_get_current_token_ctx(tokenizer_context);
  if (!token) return false;
  token_kind_t k = token->kind;
  if (k == TK_PRAGMA_PACK_PUSH) {
    pragma_pack_push_in(
        runtime_context, (int)((token_num_int_t *)token)->val);
    tk_set_current_token_ctx(tokenizer_context, token->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_POP) {
    pragma_pack_pop_in(runtime_context);
    tk_set_current_token_ctx(tokenizer_context, token->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_SET) {
    pragma_pack_set_in(
        runtime_context, (int)((token_num_int_t *)token)->val);
    tk_set_current_token_ctx(tokenizer_context, token->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_RESET) {
    pragma_pack_reset_in(runtime_context);
    tk_set_current_token_ctx(tokenizer_context, token->next);
    return true;
  }
  return false;
}

// program = funcdef*
void ps_parser_stream_begin_in_contexts(
    psx_parser_stream_t *stream,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    tokenizer_context_t *tk_ctx, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations) {
  if (!stream || !semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    abort();
  stream->semantic_context = semantic_context;
  stream->global_registry = global_registry;
  stream->local_registry = local_registry;
  stream->runtime_context = runtime_context;
  stream->toplevel_declarations = toplevel_declarations;
  tokenizer_context_t *runtime_tokenizer =
      tk_ctx ? tk_ctx : ps_parser_runtime_tokenizer(runtime_context);
  if (!runtime_tokenizer) abort();
  stream->tk_ctx = runtime_tokenizer;
  stream->previous_runtime_tokenizer_context =
      ps_parser_runtime_bind_tokenizer(runtime_context, runtime_tokenizer);
  tk_set_current_token_ctx(runtime_tokenizer, start);
  runtime_context->recoverable_syntax_error = 0;
  runtime_context->function_block_depth = 0;
  runtime_context->recovery_block_depth = 0;
}

static void psx_advance_recovery_token(
    tokenizer_context_t *tokenizer_context) {
  tk_ensure_lookahead_ctx(tokenizer_context);
  token_t *token = curtok_in(tokenizer_context);
  if (token && token->next)
    set_curtok_in(tokenizer_context, token->next);
}

static void psx_synchronize_toplevel_declaration(
    tokenizer_context_t *tokenizer_context) {
  token_t *start = curtok_in(tokenizer_context);
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  while (!tk_at_eof_ctx(tokenizer_context)) {
    token_kind_t kind = curtok_in(tokenizer_context)->kind;
    if (kind == TK_LPAREN) paren_depth++;
    else if (kind == TK_RPAREN && paren_depth > 0) paren_depth--;
    else if (kind == TK_LBRACKET) bracket_depth++;
    else if (kind == TK_RBRACKET && bracket_depth > 0) bracket_depth--;
    else if (kind == TK_LBRACE) brace_depth++;
    else if (kind == TK_RBRACE && brace_depth > 0) brace_depth--;
    psx_advance_recovery_token(tokenizer_context);
    if (kind == TK_SEMI && paren_depth == 0 && bracket_depth == 0 &&
        brace_depth == 0)
      break;
    if (kind == TK_RBRACE && paren_depth == 0 && bracket_depth == 0 &&
        brace_depth == 0)
      break;
  }
  if (curtok_in(tokenizer_context) == start &&
      !tk_at_eof_ctx(tokenizer_context))
    psx_advance_recovery_token(tokenizer_context);
}

int ps_parse_next_toplevel_item(
    psx_parser_stream_t *stream, psx_parsed_toplevel_item_t *item) {
  if (!stream || !stream->semantic_context || !item) return 0;
  psx_semantic_context_t *semantic_context = stream->semantic_context;
  tokenizer_context_t *tokenizer_context = stream->tk_ctx;
  if (!tokenizer_context) return 0;
  *item = (psx_parsed_toplevel_item_t){0};
  while (!tk_at_eof_ctx(tokenizer_context)) {
    if (psx_try_consume_pragma_pack_marker_in(stream->runtime_context))
      continue;
    if (curtok_in(tokenizer_context)->kind == TK_STATIC_ASSERT) {
      item->kind = PSX_TOPLEVEL_ITEM_STATIC_ASSERT;
      psx_parse_static_assert_syntax_in_contexts(
          &item->value.static_assertion,
          semantic_context, stream->global_registry,
          stream->local_registry,
          stream->runtime_context,
          stream->toplevel_declarations
              ? &stream->toplevel_declarations->name_classifier
              : NULL,
          NULL);
      return 1;
    }
    psx_parsed_toplevel_declaration_t declaration = {0};
    if (!psx_parse_toplevel_declaration_head_syntax_in_contexts(
            &declaration, semantic_context, stream->global_registry,
            stream->local_registry, stream->runtime_context,
            stream->toplevel_declarations
                ? &stream->toplevel_declarations->name_classifier
                : NULL)) {
      ps_dispose_toplevel_declaration_syntax(&declaration);
      psx_synchronize_toplevel_declaration(tokenizer_context);
      if (diag_limit_kind_in(diagnostics(semantic_context))) break;
      continue;
    }
    psx_skip_gnu_attributes_ctx(tokenizer_context);
    if (!declaration.is_standalone_tag &&
        curtok_in(tokenizer_context)->kind == TK_LBRACE) {
      psx_parsed_declarator_t *declarator = &declaration.declarators[0];
      if (declaration.is_typedef ||
          declarator->function_suffix_count <= 0) {
        ps_diag_ctx_in(
            diagnostics(semantic_context), declarator->diagnostic_token,
            "funcdef", "%s",
            diag_message_for_in(
                diagnostics(semantic_context),
                DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
      }
      item->kind = PSX_TOPLEVEL_ITEM_FUNCTION_HEADER;
      psx_move_toplevel_declaration_head_to_function_definition(
          &declaration, &item->value.function_header);
    } else {
      item->kind = PSX_TOPLEVEL_ITEM_DECLARATION;
      item->value.declaration = declaration;
      if (!psx_finish_toplevel_declaration_syntax_in_contexts(
              &item->value.declaration,
              stream->toplevel_declarations, semantic_context,
              stream->global_registry,
              stream->local_registry, stream->runtime_context)) {
        ps_dispose_toplevel_declaration_syntax(&item->value.declaration);
        item->kind = PSX_TOPLEVEL_ITEM_EOF;
        psx_synchronize_toplevel_declaration(tokenizer_context);
        if (diag_limit_kind_in(diagnostics(semantic_context))) break;
        continue;
      }
    }
    return 1;
  }
  item->kind = PSX_TOPLEVEL_ITEM_EOF;
  return 0;
}

void ps_parser_stream_end(psx_parser_stream_t *stream) {
  if (stream) {
    if (stream->runtime_context &&
        stream->previous_runtime_tokenizer_context) {
      ps_parser_runtime_bind_tokenizer(
          stream->runtime_context,
          stream->previous_runtime_tokenizer_context);
    }
    stream->tk_ctx = NULL;
    stream->previous_runtime_tokenizer_context = NULL;
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

static void emit_invalid_type_spec_diag(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context) {
  diag_emit_tokf_in(
      diagnostics(semantic_context),
      DIAG_ERR_PARSER_INVALID_TYPE_SPEC, curtok_in(tokenizer_context), "%s",
      diag_message_for_in(
          diagnostics(semantic_context),
          DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
/* 後置 cv/atomic 修飾子トークンを 1 つ消費する。const/volatile/restrict/atomic
 * いずれも同じ「対応 flag を立てて trailing トークンを進める」パターンなので
 * 集約する。消費したら 1、該当しなければ 0 (呼出側で loop を抜ける)。 */
static int try_consume_post_cv_qualifier(
    tokenizer_context_t *tokenizer_context,
    psx_type_spec_result_t *out, token_kind_t k) {
  switch (k) {
    case TK_CONST:    out->is_const_qualified = 1; break;
    case TK_VOLATILE: out->is_volatile_qualified = 1; break;
    case TK_RESTRICT: break;
    case TK_ATOMIC:   out->is_atomic = 1; break;
    default: return 0;
  }
  set_curtok_in(
      tokenizer_context, curtok_in(tokenizer_context)->next);
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

token_kind_t psx_consume_type_kind_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context,
    psx_type_spec_result_t *out) {
  psx_type_spec_syntax_t syntax = {
      .tokenizer_context = tokenizer_context,
  };
  return psx_consume_type_kind_with_syntax_ex(
      semantic_context, out, &syntax);
}

token_kind_t psx_consume_type_kind_with_syntax_ex(
    psx_semantic_context_t *semantic_context,
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax) {
  if (!semantic_context || !syntax || !syntax->tokenizer_context)
    return TK_EOF;
  tokenizer_context_t *tokenizer_context = syntax->tokenizer_context;
  psx_type_spec_result_t local;
  if (!out) out = &local;
  skip_cv_qualifiers_into_ex(
      semantic_context, tokenizer_context, out, syntax);
  if (curtok_in(tokenizer_context)->kind == TK_ATOMIC &&
      curtok_in(tokenizer_context)->next &&
      curtok_in(tokenizer_context)->next->kind == TK_LPAREN) {
    out->is_atomic = 1;
    token_kind_t inner = parse_atomic_type_specifier(
        semantic_context, tokenizer_context);
    if (inner != TK_EOF) {
      out->kind = inner;
      return inner;
    }
  }
  // qualifier-form: _Atomic int x;
  if (curtok_in(tokenizer_context)->kind == TK_ATOMIC) {
    out->is_atomic = 1;
    set_curtok_in(
        tokenizer_context, curtok_in(tokenizer_context)->next);
  }
  token_t *start = curtok_in(tokenizer_context);
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
    token_kind_t k = curtok_in(tokenizer_context)->kind;
    if (k == TK_COMPLEX) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_complex = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_IMAGINARY) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_imaginary = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_SIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_signed = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_UNSIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_unsigned = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_LONG) {
      if (saw_char || saw_short || saw_void || saw_float || saw_bool || long_count >= 2) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      long_count++;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_SHORT) {
      if (saw_char || saw_short || long_count || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_short = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_INT) {
      if (saw_int || saw_char || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_int = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_CHAR) {
      if (saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_char = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_VOID) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_void = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_FLOAT) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_double || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_float = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_DOUBLE) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || saw_int || saw_void || saw_float || saw_bool) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_double = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    if (k == TK_BOOL) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double) {
        emit_invalid_type_spec_diag(semantic_context, tokenizer_context);
      }
      saw_bool = 1;
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
      continue;
    }
    // 後置 cv 修飾子（int const, volatile int const など）は同じ形なので集約。
    if (try_consume_post_cv_qualifier(tokenizer_context, out, k)) continue;
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
        ps_diag_ctx_in(
            diagnostics(semantic_context), curtok_in(tokenizer_context),
            "decl",
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
        if (curtok_in(tokenizer_context)->next &&
            curtok_in(tokenizer_context)->next->kind == TK_LPAREN)
          break;
        out->is_atomic = 1;
      }
      /* TK_AUTO / TK_REGISTER / TK_INLINE / TK_NORETURN / TK_ALIGNAS(...) は flag を立てずに
       * 単純消費。TK_ALIGNAS は `(value)` 形のため複雑だが、型指定子の後の出現は稀 (実例は
       * `int _Alignas(8) x` で C11 では基本的に typespec の前)。ここでは省略 — 必要ならば
       * 既存の skip_cv_qualifiers の TK_ALIGNAS 分岐を引用する。 */
      set_curtok_in(
          tokenizer_context, curtok_in(tokenizer_context)->next);
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
      diag_emit_tokf_in(
          diagnostics(semantic_context),
          DIAG_ERR_PARSER_INVALID_CONTEXT, start, "%s",
          diag_message_for_in(
              diagnostics(semantic_context),
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
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    tokenizer_context_t *tokenizer_context,
    const psx_local_declaration_callbacks_t *local_declarations) {
  ps_ctx_enter_block_scope_in(semantic_context);
  ps_parser_enter_recovery_block_in(runtime_context);
  node_block_t *body = arena_alloc_in(
      ps_parser_runtime_arena(runtime_context), sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t *));
  while (!tk_consume_ctx(tokenizer_context, '}')) {
    // #pragma pack マーカーは関数本体冒頭・任意の位置で出現しうる。透過処理。
    if (psx_try_consume_pragma_pack_marker_in(runtime_context)) continue;
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap_in(
          ps_parser_runtime_diagnostics(runtime_context),
          body_cap, i + 2);
      body->body = pda_xreallocarray_in(
          ps_parser_runtime_diagnostics(runtime_context), body->body,
          (size_t)body_cap, sizeof(node_t *));
    }
    token_t *stmt_tok = curtok_in(tokenizer_context);
    psx_lvar_usage_region_t *region =
        psx_decl_begin_lvar_usage_region_in(local_registry);
    body->body[i] = psx_stmt_stmt_in_contexts(
        semantic_context, global_registry, local_registry,
        runtime_context,
        local_declarations ? &local_declarations->name_classifier : NULL,
        local_declarations);
    psx_decl_end_lvar_usage_region_in(local_registry, region);
    if (ps_parser_has_recoverable_syntax_error_in(runtime_context)) {
      body->body[i] = NULL;
      ps_parser_leave_recovery_block_in(runtime_context);
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
  ps_parser_leave_recovery_block_in(runtime_context);
  ps_ctx_leave_block_scope_in(semantic_context);
  return body;
}

node_t *ps_parse_function_definition_body(
    psx_parser_stream_t *stream, node_function_definition_t *function,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!stream || !stream->semantic_context || !stream->global_registry ||
      !stream->local_registry || !stream->runtime_context || !function)
    return NULL;
  psx_semantic_context_t *semantic_context = stream->semantic_context;
  psx_parser_runtime_context_t *runtime = stream->runtime_context;
  tokenizer_context_t *tokenizer_context = stream->tk_ctx;
  if (!tokenizer_context) return NULL;
  runtime->recoverable_syntax_error = 0;
  runtime->recovery_block_depth = 0;
  tk_expect_ctx(tokenizer_context, '{');
  function->base.rhs =
      (node_t *)parse_funcdef_body_block(
          semantic_context, stream->global_registry,
          stream->local_registry, stream->runtime_context,
          tokenizer_context,
          local_declarations);
  if (runtime->recoverable_syntax_error) {
    int depth = runtime->recovery_block_depth > 0
        ? runtime->recovery_block_depth : 1;
    while (!tk_at_eof_ctx(tokenizer_context) && depth > 0) {
      token_kind_t kind = curtok_in(tokenizer_context)->kind;
      if (kind == TK_LBRACE) depth++;
      else if (kind == TK_RBRACE) depth--;
      psx_advance_recovery_token(tokenizer_context);
    }
    ps_decl_set_current_funcname_in(stream->local_registry, NULL, 0);
    runtime->recoverable_syntax_error = 0;
    runtime->recovery_block_depth = 0;
    return NULL;
  }
  psx_ctx_validate_goto_refs_in(semantic_context);
  function->lvars = ps_decl_get_locals_in(stream->local_registry);
  ps_decl_set_current_funcname_in(stream->local_registry, NULL, 0);
  return (node_t *)function;
}

node_t *ps_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations,
    tokenizer_context_t *tk_ctx, token_t *start) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  tokenizer_context_t *runtime_tokenizer =
      tk_ctx ? tk_ctx : ps_parser_runtime_tokenizer(runtime_context);
  if (!runtime_tokenizer) return NULL;
  tokenizer_context_t *previous_runtime_tokenizer =
      ps_parser_runtime_bind_tokenizer(runtime_context, runtime_tokenizer);
  tk_set_current_token_ctx(runtime_tokenizer, start);
  node_t *node = psx_expr_expr_in_contexts(
      semantic_context, global_registry, local_registry,
      runtime_context,
      local_declarations ? &local_declarations->name_classifier : NULL,
      local_declarations);
  if (previous_runtime_tokenizer) {
    ps_parser_runtime_bind_tokenizer(
        runtime_context, previous_runtime_tokenizer);
  }
  return node;
}
void ps_parser_mark_recoverable_syntax_error_in(
    psx_parser_runtime_context_t *runtime_context) {
  if (!runtime_context) return;
  if (!runtime_context->recoverable_syntax_error)
    runtime_context->recovery_block_depth =
        runtime_context->function_block_depth;
  runtime_context->recoverable_syntax_error = 1;
}

int ps_parser_has_recoverable_syntax_error_in(
    const psx_parser_runtime_context_t *runtime_context) {
  return runtime_context ? runtime_context->recoverable_syntax_error : 0;
}

void ps_parser_enter_recovery_block_in(
    psx_parser_runtime_context_t *runtime_context) {
  if (runtime_context) runtime_context->function_block_depth++;
}

void ps_parser_leave_recovery_block_in(
    psx_parser_runtime_context_t *runtime_context) {
  if (runtime_context && runtime_context->function_block_depth > 0)
    runtime_context->function_block_depth--;
}
