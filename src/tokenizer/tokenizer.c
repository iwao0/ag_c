#include "allocator.h"
#include "../branch_hint.h"
#include "tokenizer.h"
#include "../diag/diag.h"
#include "charclass.h"
#include "context.h"
#include "diag_helper.h"
#include "keywords.h"
#include "literals.h"
#include "number.h"
#include "punctuator.h"
#include "scanner.h"
#include "trigraph.h"
#if !defined(AGC_TARGET_WASM32) && !defined(__wasm32__)
#include <setjmp.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* 実行中セッションの active context。未設定 (非トークナイズ中) では既定 context。 */
static tokenizer_context_t *tk_tokenize_ctx_active = NULL;

/* トークンストリーム経路で、パーサがカーソルを前進させるたびに呼ばれるフック。
 * 先読み分を materialize し、通り過ぎたトークンを解放する (driver が登録/解除)。
 * 非ストリーム経路では NULL なので無影響。カーソル更新の 2 経路
 * (逐次前進 tk_advance_current_token と明示ジャンプ tk_set_current_token_ctx)
 * の両方で呼ぶ。 */
void tk_set_cursor_hook_ctx(tokenizer_context_t *ctx, void (*fn)(token_t *)) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx) use_ctx->cursor_hook = fn;
}

void tk_set_cursor_hook(void (*fn)(token_t *)) {
  tk_set_cursor_hook_ctx(NULL, fn);
}

void (*tk_get_cursor_hook_ctx(tokenizer_context_t *ctx))(token_t *) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  return use_ctx ? use_ctx->cursor_hook : NULL;
}

void (*tk_get_cursor_hook(void))(token_t *) {
  return tk_get_cursor_hook_ctx(NULL);
}

tokenizer_context_t *tk_runtime_ctx(void) {
  return tk_tokenize_ctx_active ? tk_tokenize_ctx_active : tk_context_active();
}

tokenizer_context_t *tk_effective_ctx(tokenizer_context_t *ctx) {
  return ctx ? ctx : tk_runtime_ctx();
}

void tk_advance_current_token(tokenizer_context_t *ctx, token_t *cur) {
  ctx = tk_effective_ctx(ctx);
  if (!ctx) return;
  token_t *nx = cur ? cur->next : NULL;
  ctx->current_token = nx;
  if (ctx->cursor_hook && nx) ctx->cursor_hook(nx);
}

/* カーソルを進めない深い前方先読み (パーサの _Generic 型照合等) の直前に呼び、ストリーミング
 * 生成器に前方 lookahead を満たさせるフック。プリプロセッサが登録する。未登録 (非ストリーム
 * の単体テスト等) では no-op。parser↔preprocess を直接リンクせず疎結合に保つための間接化。 */
void tk_set_ensure_lookahead_hook_ctx(
    tokenizer_context_t *ctx, void (*fn)(void)) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx) use_ctx->ensure_lookahead_hook = fn;
}

void tk_set_ensure_lookahead_hook(void (*fn)(void)) {
  tk_set_ensure_lookahead_hook_ctx(NULL, fn);
}

void tk_ensure_lookahead_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx && use_ctx->ensure_lookahead_hook)
    use_ctx->ensure_lookahead_hook();
}

void tk_ensure_lookahead(void) { tk_ensure_lookahead_ctx(NULL); }

token_t *tk_get_current_token(void) {
  return tk_get_current_token_ctx(NULL);
}

token_t *tk_get_current_token_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  return use_ctx ? use_ctx->current_token : NULL;
}

void tk_set_current_token(token_t *tok) {
  tk_set_current_token_ctx(NULL, tok);
}

void tk_set_current_token_ctx(tokenizer_context_t *ctx, token_t *tok) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx) {
    use_ctx->current_token = tok;
  }
  if (use_ctx && use_ctx->cursor_hook && tok) use_ctx->cursor_hook(tok);
}

const char *tk_get_user_input_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  return use_ctx ? use_ctx->user_input : NULL;
}

void tk_set_user_input_ctx(tokenizer_context_t *ctx, const char *p) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx) {
    use_ctx->user_input = p;
  }
}

/** @brief Tokenizer統計の計測基準点をリセットする。 */
void tk_reset_tokenizer_stats(void) {
  tokenizer_context_t *ctx = tk_runtime_ctx();
  ctx->stats_base_chunks = tk_allocator_total_chunks();
  ctx->stats_base_reserved_bytes = tk_allocator_total_reserved_bytes();
}

/** @brief Tokenizer統計（alloc回数/bytes）を取得する。 */
tokenizer_stats_t tk_get_tokenizer_stats(void) {
  tokenizer_context_t *ctx = tk_runtime_ctx();
  tokenizer_stats_t stats = {0};
  stats.alloc_count = tk_allocator_total_chunks() - ctx->stats_base_chunks;
  stats.alloc_bytes =
      tk_allocator_total_reserved_bytes() - ctx->stats_base_reserved_bytes;
  stats.peak_alloc_bytes = stats.alloc_bytes;
  return stats;
}

const char *tk_get_filename_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  return use_ctx ? use_ctx->current_filename : NULL;
}

void tk_set_filename_ctx(tokenizer_context_t *ctx, const char *name) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx) {
    use_ctx->current_filename = name;
  }
}

static void *tcalloc(size_t n, size_t size) {
  return tk_allocator_calloc(n, size);
}

static int checked_span_len(char *start, char *end, const char *what) {
  ptrdiff_t diff = end - start;
  tokenizer_context_t *ctx = tk_runtime_ctx();
  if (diff < 0 || (size_t)diff > ctx->max_token_len_for_test ||
      (size_t)diff > (size_t)INT_MAX) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME, start, "%s", diag_message_for(DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME), (char *)what);
  }
  return (int)diff;
}

void tk_set_max_token_len_limit_for_test(size_t max_len) {
  tokenizer_context_t *ctx = tk_runtime_ctx();
  ctx->max_token_len_for_test =
      (max_len == 0) ? (size_t)INT_MAX : max_len;
}

// 新しいトークンを作成して、curに繋げる
static void init_token_base(token_t *tok, token_kind_t kind, int line_no,
                            const char *loc, int byte_length) {
  tok->kind = kind;
  tokenizer_context_t *ctx = tk_runtime_ctx();
  tok->file_name_id = tk_filename_intern(ctx ? ctx->current_filename : NULL);
  tok->line_no = line_no;
  tok->byte_offset = -1;
  tok->byte_length = byte_length < 0 ? 0 : byte_length;
  const char *input = tk_get_user_input_ctx(ctx);
  tok->source_input = input;
  if (input && loc && loc >= input) {
    ptrdiff_t offset = loc - input;
    if ((size_t)offset <= (size_t)INT_MAX) tok->byte_offset = (int)offset;
  }
}

static token_t *new_token_simple(token_kind_t kind, token_t *cur, int line_no,
                                 bool at_bol, bool has_space, const char *loc,
                                 int byte_length) {
  token_pp_t *tok = tcalloc(1, sizeof(token_pp_t));
  init_token_base(&tok->base, kind, line_no, loc, byte_length);
  tok->base.at_bol = at_bol;
  tok->base.has_space = has_space;
  cur->next = (token_t *)tok;
  return (token_t *)tok;
}

static token_ident_t *new_token_ident(token_t *cur, char *str, int len, int line_no,
                                      bool at_bol, bool has_space, const char *loc,
                                      int byte_length) {
  token_ident_t *tok = tcalloc(1, sizeof(token_ident_t));
  init_token_base(&tok->pp.base, TK_IDENT, line_no, loc, byte_length);
  tok->pp.base.at_bol = at_bol;
  tok->pp.base.has_space = has_space;
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_string_t *new_token_string(token_t *cur, char *str, int len, int line_no,
                                        bool at_bol, bool has_space, const char *loc,
                                        int byte_length);
static token_num_int_t *new_token_num_int(
    token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space,
    const char *loc, int byte_length);
static token_num_float_t *new_token_num_float(
    token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space,
    const char *loc, int byte_length);
typedef struct tokenize_flags_t tokenize_flags_t;
typedef struct tokenize_session_t tokenize_session_t;

struct tokenize_flags_t {
  bool at_bol;
  bool has_space;
};

struct tokenize_session_t {
  tokenizer_context_t *prev_ctx;
  tokenizer_context_t *ctx;
};

/** @brief 実装定義: 通常/接頭辞付きともにマルチ文字文字定数を受理する。 */
static inline bool tk_accept_multichar_char_constant(void) {
  return true;
}

/**
 * @brief 文字列リテラル（接頭辞含む）を読み取り、トークンを生成する。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param cur_io トークン連結末尾ポインタ。成功時は新規トークンへ更新。
 * @param line_no 現在行番号。
 * @param at_bol 行頭フラグ（生成トークンへ転写）。
 * @param has_space 直前空白フラグ（生成トークンへ転写）。
 * @return 文字列リテラルを受理した場合 `true`。非該当時は `false`（非破壊）。
 * @warning 未終端文字列は診断終了する。
 */
static bool tokenize_string_literal(
    char **pp,
    token_t **cur_io,
    int line_no,
    bool at_bol,
    bool has_space) {
  char *token_start = *pp;
  char *p = token_start;
  int str_prefix = 0;
  tk_string_prefix_kind_t str_prefix_kind = TK_STR_PREFIX_NONE;
  tk_char_width_t str_char_width = TK_CHAR_WIDTH_CHAR;
  bool is_string_lit = (*p == '"');
  tk_parse_string_prefix(p, &str_prefix, &str_prefix_kind, &str_char_width);
  if (str_prefix > 0) is_string_lit = true;
  if (!is_string_lit) return false;

  if (*p == '"') {
    str_prefix = 0;
    str_prefix_kind = TK_STR_PREFIX_NONE;
    str_char_width = TK_CHAR_WIDTH_CHAR;
  }
  p += str_prefix;
  p++; // opening quote
  char *start = p;
  while (true) {
    if (*p == '\0' || *p == '\n') {
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED, p, "%s",
                  diag_message_for(DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED));
    }
    if (*p == '"') break;
    if (*p == '\\') {
      p++;
      tk_skip_escape_in_literal(&p);
      continue;
    }
    p++;
  }
  int len = checked_span_len(start, p, "文字列リテラル");
  p++; // closing quote
  token_string_t *st = new_token_string(*cur_io, start, len, line_no, at_bol, has_space,
                                        token_start, checked_span_len(token_start, p, "文字列リテラル"));
  st->char_width = str_char_width;
  st->str_prefix_kind = str_prefix_kind;
  *cur_io = (token_t *)st;
  *pp = p;
  return true;
}

/**
 * @brief 文字定数（接頭辞含む）を読み取り、数値トークンを生成する。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param cur_io トークン連結末尾ポインタ。成功時は新規トークンへ更新。
 * @param line_no 現在行番号。
 * @param at_bol 行頭フラグ（生成トークンへ転写）。
 * @param has_space 直前空白フラグ（生成トークンへ転写）。
 * @return 文字定数を受理した場合 `true`。非該当時は `false`（非破壊）。
 * @warning 空文字/未終端/不正エスケープは診断終了する。
 */
static bool tokenize_char_literal(
    char **pp,
    token_t **cur_io,
    int line_no,
    bool at_bol,
    bool has_space) {
  char *p = *pp;
  int chr_prefix = 0;
  tk_char_prefix_kind_t chr_prefix_kind = TK_CHAR_PREFIX_NONE;
  tk_char_width_t chr_char_width = TK_CHAR_WIDTH_CHAR;
  bool is_char_lit = (*p == '\'');
  tk_parse_char_prefix(p, &chr_prefix, &chr_prefix_kind, &chr_char_width);
  if (chr_prefix > 0) is_char_lit = true;
  if (!is_char_lit) return false;

  char *start = p;
  p += chr_prefix;
  p++; // opening quote
  if (*p == '\0' || *p == '\n') {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_UNTERMINATED, p, "%s",
                diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_UNTERMINATED));
  }
  if (*p == '\'') {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY, p, "%s",
                diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY));
  }
  unsigned long long ch = 0;
  int nchar = 0;
  if (!tk_accept_multichar_char_constant()) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID, p, "%s",
                diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID));
  }
  while (*p && *p != '\'') {
    int one = 0;
    if (*p == '\\') {
      p++;
      one = tk_read_escape_char(&p);
    } else if (*p == '\n') {
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID, p, "%s",
                  diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID));
    } else {
      one = (unsigned char)*p;
      p++;
    }
    ch = ((ch << 8) | (unsigned)(one & 0xFF)) & 0xFFFFFFFFULL;
    nchar++;
  }
  if (nchar == 0 || *p != '\'') {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID, p, "%s",
                diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID));
  }
  p++; // closing quote
  int len = checked_span_len(start, p, "文字リテラル");
  token_num_int_t *num = new_token_num_int(*cur_io, start, len, line_no, at_bol, has_space,
                                           start, len);
  num->base.num_kind = TK_NUM_KIND_INT;
  num->uval = ch;
  num->val = (long long)ch;
  num->int_base = 10;
  num->is_unsigned = false;
  num->int_size = TK_INT_SIZE_INT;
  num->char_width = chr_char_width;
  num->char_prefix_kind = chr_prefix_kind;
  *cur_io = (token_t *)num;
  *pp = p;
  return true;
}

/**
 * @brief 記号（最長一致の複数文字 + 1文字）を読み取り、トークンを生成する。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param cur_io トークン連結末尾ポインタ。成功時は新規トークンへ更新。
 * @param line_no 現在行番号。
 * @param at_bol 行頭フラグ（生成トークンへ転写）。
 * @param has_space 直前空白フラグ（生成トークンへ転写）。
 * @return 記号を受理した場合 `true`。非該当時は `false`（非破壊）。
 */
static bool tokenize_punctuator(
    char **pp, token_t **cur_io, int line_no, bool at_bol, bool has_space) {
  char *p = *pp;
  token_kind_t matched_kind = TK_EOF;
  int matched_len = 0;
  if (match_punctuator(p, &matched_kind, &matched_len) && matched_len >= 2) {
    *cur_io = new_token_simple(matched_kind, *cur_io, line_no, at_bol, has_space, p, matched_len);
    *pp = p + matched_len;
    return true;
  }

  if (tk_is_punctuator1(*p) || (*p == '.' && !tk_is_digit(p[1]))) {
    token_kind_t kind = punctuator_kind_for_char(*p);
    *cur_io = new_token_simple(kind, *cur_io, line_no, at_bol, has_space, p, 1);
    *pp = p + 1;
    return true;
  }
  return false;
}

/**
 * @brief 識別子/キーワードを読み取り、該当トークンを生成する。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param cur_io トークン連結末尾ポインタ。成功時は新規トークンへ更新。
 * @param line_no 現在行番号。
 * @param at_bol 行頭フラグ（生成トークンへ転写）。
 * @param has_space 直前空白フラグ（生成トークンへ転写）。
 * @return 識別子/キーワードを受理した場合 `true`。非該当時は `false`（非破壊）。
 */
static bool tokenize_ident_or_keyword(
    char **pp, token_t **cur_io, int line_no, bool at_bol, bool has_space) {
  char *p = *pp;
  int adv = 0;
  if (!LIKELY(tk_scan_ident_start(p, &adv))) {
    return false;
  }

  char *start = p;
  bool has_ucn_escape = (adv > 1);
  p += adv;
  while (tk_scan_ident_continue(p, &adv)) {
    if (adv > 1) has_ucn_escape = true;
    p += adv;
  }

  int len = checked_span_len(start, p, "識別子");
  char *id_str = start;
  int id_len = len;
  bool has_ucn = false;
  if (has_ucn_escape) {
    tk_decode_identifier_ucn(start, len, &id_str, &id_len, &has_ucn);
  }

  token_kind_t kw_kind = TK_EOF;
  if (!has_ucn) {
    kw_kind = lookup_keyword(start, len);
  }

  if (kw_kind != TK_EOF) {
    *cur_io = new_token_simple(kw_kind, *cur_io, line_no, at_bol, has_space, start, len);
  } else {
    token_ident_t *id = new_token_ident(*cur_io, id_str, id_len, line_no, at_bol, has_space,
                                        start, len);
    *cur_io = (token_t *)id;
  }
  *pp = p;
  return true;
}

/**
 * @brief 数値リテラルを読み取り、整数/浮動小数トークンを生成する。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param cur_io トークン連結末尾ポインタ。成功時は新規トークンへ更新。
 * @param line_no 現在行番号。
 * @param at_bol 行頭フラグ（生成トークンへ転写）。
 * @param has_space 直前空白フラグ（生成トークンへ転写）。
 * @return 数値リテラルを受理した場合 `true`。非該当時は `false`（非破壊）。
 * @warning 不正サフィックス/範囲外などは診断終了する。
 */
static bool tokenize_number_literal(
    char **pp, token_t **cur_io, int line_no, bool at_bol, bool has_space);

/**
 * @brief Tokenizer実行セッションを開始し、active contextを切り替える。
 * @param ctx 実行対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 復元用のセッション情報。
 */
static tokenize_session_t begin_tokenize_session(tokenizer_context_t *ctx) {
  tokenize_session_t s = {0};
  s.prev_ctx = tk_tokenize_ctx_active;
  s.ctx = ctx ? ctx : tk_context_active();
  tk_tokenize_ctx_active = s.ctx;
  tk_set_current_token_ctx(s.ctx, NULL);
  return s;
}

/**
 * @brief 現在位置の1トークン分を切り出して進める。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param cur_io トークン連結末尾ポインタ。成功時は新規トークンへ更新。
 * @param line_no 現在行番号。
 * @param at_bol 行頭フラグ。
 * @param has_space 直前空白フラグ。
 * @return 生成したトークン。非該当時は NULL。
 */
static token_t *tokenize_one(char **pp, token_t **cur_io, int line_no, bool at_bol, bool has_space) {
  if (tokenize_punctuator(pp, cur_io, line_no, at_bol, has_space)) return *cur_io;
  if (tokenize_string_literal(pp, cur_io, line_no, at_bol, has_space)) return *cur_io;
  if (tokenize_char_literal(pp, cur_io, line_no, at_bol, has_space)) return *cur_io;
  if (tokenize_ident_or_keyword(pp, cur_io, line_no, at_bol, has_space)) return *cur_io;
  if (tokenize_number_literal(pp, cur_io, line_no, at_bol, has_space)) return *cur_io;
  return NULL;
}

/**
 * @brief Tokenizer実行セッションを終了し、カーソル確定とcontext復元を行う。
 * @param s 開始時に取得したセッション情報。
 * @param head_next 生成トークン列の先頭。
 * @return `head_next` をそのまま返す。
 */
static token_t *end_tokenize_session(tokenize_session_t *s, token_t *head_next) {
  tk_set_current_token(head_next);
  tk_tokenize_ctx_active = s->prev_ctx;
  return head_next;
}

static token_string_t *new_token_string(token_t *cur, char *str, int len, int line_no,
                                        bool at_bol, bool has_space, const char *loc,
                                        int byte_length) {
  token_string_t *tok = tcalloc(1, sizeof(token_string_t));
  init_token_base(&tok->pp.base, TK_STRING, line_no, loc, byte_length);
  tok->pp.base.at_bol = at_bol;
  tok->pp.base.has_space = has_space;
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_num_int_t *new_token_num_int(
    token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space,
    const char *loc, int byte_length) {
  token_num_int_t *tok = tcalloc(1, sizeof(token_num_int_t));
  init_token_base(&tok->base.pp.base, TK_NUM, line_no, loc, byte_length);
  tok->base.pp.base.at_bol = at_bol;
  tok->base.pp.base.has_space = has_space;
  tok->base.str = str;
  tok->base.len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_num_float_t *new_token_num_float(
    token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space,
    const char *loc, int byte_length) {
  token_num_float_t *tok = tcalloc(1, sizeof(token_num_float_t));
  init_token_base(&tok->base.pp.base, TK_NUM, line_no, loc, byte_length);
  tok->base.pp.base.at_bol = at_bol;
  tok->base.pp.base.has_space = has_space;
  tok->base.str = str;
  tok->base.len = len;
  cur->next = (token_t *)tok;
  return tok;
}


static bool tokenize_number_literal(
    char **pp, token_t **cur_io, int line_no, bool at_bol, bool has_space) {
  char *p = *pp;
  if (!(tk_is_digit(*p) || (*p == '.' && tk_is_digit(p[1])))) {
    return false;
  }

  char *start = p;
  parsed_num_t parsed = {0};
  tk_parse_number_literal(&p, &parsed);
  int len = checked_span_len(start, p, "数値リテラル");
  if (parsed.fp_kind == TK_FLOAT_KIND_NONE) {
    token_num_int_t *num = new_token_num_int(*cur_io, start, len, line_no, at_bol, has_space,
                                             start, len);
    num->base.num_kind = TK_NUM_KIND_INT;
    num->val = parsed.val;
    num->uval = parsed.uval;
    num->is_unsigned = parsed.is_unsigned;
    num->int_size = parsed.int_size;
    num->int_base = parsed.int_base;
    num->char_width = parsed.char_width;
    num->char_prefix_kind = parsed.char_prefix_kind;
    *cur_io = (token_t *)num;
  } else {
    token_num_float_t *num = new_token_num_float(*cur_io, start, len, line_no, at_bol, has_space,
                                                 start, len);
    num->base.num_kind = TK_NUM_KIND_FLOAT;
    num->fval = parsed.fval;
    num->fp_kind = parsed.fp_kind;
    num->float_suffix_kind = parsed.float_suffix_kind;
    *cur_io = (token_t *)num;
  }
  *pp = p;
  return true;
}

/** @brief 入力文字列をトークナイズし、先頭トークンを返す。 */
token_t *tk_tokenize(const char *p) {
  return tk_tokenize_ctx(NULL, p);
}

/** @brief 入力文字列を正規化し、診断用入力参照をコンテキストへ設定する。 */
static char *tokenize_prepare_input(tokenizer_context_t *ctx, const char *in) {
  tk_allocator_set_expected_size(strlen(in));
  char *normalized = tk_replace_trigraphs(in);
  tk_set_user_input_ctx(ctx, normalized);
  return normalized;
}

/* 遅延 (pull 型) トークナイザ。入力を一括で全トークン化せず、tk_stream_next を呼ぶたびに
 * 1 トークンだけ切り出す。状態 (カーソル p・行頭/空白フラグ・行番号・セッション) を保持し、
 * トークンストリーム化 (字句解析→プリプロセス→パースを 1 関数ずつ流す) の基盤にする。
 * 現状は tk_tokenize_ctx がこの上で全トークンを生成し、挙動はバッチ版と同一。 */
struct tk_token_stream {
  tokenize_session_t session;
  char *p;          // 正規化済み入力へのカーソル
  bool at_bol;
  bool has_space;
  int line_no;
  bool done;        // TK_EOF を生成済み
};

void tk_stream_open(tk_token_stream_t *s, tokenizer_context_t *ctx, const char *in) {
  s->session = begin_tokenize_session(ctx);
  s->p = tokenize_prepare_input(s->session.ctx, in);
  s->at_bol = true;
  s->has_space = false;
  s->line_no = 1;
  s->done = false;
}

/* `#if 0` 偽分岐の読み飛ばし中は、トークナイズ不能な文字 (` @ $) ・未終端リテラル
 * (`don't` の `'`, 閉じない `"`) ・不正数値 (`123abc`) で即エラーにせず、当該トークンを
 * 1 文字の TK_UNKNOWN にして進める。C の翻訳フェーズ 3 では偽分岐の中身も pp-token に分解
 * されるだけで、これらはフェーズ 7 まで到達しないのでエラーにならない。プリプロセッサが
 * pps_skip_cond_incl / pps_materialize_line の先読み区間だけ true にする。
 * scanner が出す各種 TK_DIAG_* は (寛容モード中のみ) longjmp で tk_stream_next へ巻き戻る。 */
void tk_set_tolerate_untokenizable_ctx(tokenizer_context_t *ctx, bool v) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  if (use_ctx) use_ctx->tolerate_untokenizable = v;
}

void tk_set_tolerate_untokenizable(bool v) {
  tk_set_tolerate_untokenizable_ctx(NULL, v);
}

/* TK_DIAG_* マクロから呼ばれる。寛容モードかつ tokenize_one 実行中なら、エラーを出さず
 * tk_stream_next の setjmp 地点へ巻き戻す (戻らない)。それ以外は何もしない (通常診断へ続く)。 */
void tk_tolerate_longjmp_if_active(void) {
#if !defined(AGC_TARGET_WASM32) && !defined(__wasm32__)
  tokenizer_context_t *ctx = tk_runtime_ctx();
  if (ctx && ctx->tolerate_untokenizable && ctx->tolerate_jump_target)
    longjmp(*(jmp_buf *)ctx->tolerate_jump_target, 1);
#endif
}

#if defined(AGC_TARGET_WASM32) || defined(__wasm32__)
static bool tolerant_skip_escape(char **pp) {
  char *p = *pp;
  switch (*p) {
    case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
    case '\\': case '\'': case '"': case '?':
      *pp = p + 1;
      return true;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
      int cnt = 0;
      while (cnt < 3 && tk_is_octal_digit(*p)) {
        p++;
        cnt++;
      }
      *pp = p;
      return true;
    }
    case 'x':
      p++;
      if (!tk_is_xdigit(*p)) return false;
      while (tk_is_xdigit(*p)) p++;
      *pp = p;
      return true;
    case 'u':
    case 'U': {
      int digits = (*p == 'u') ? 4 : 8;
      uint32_t cp = 0;
      for (int i = 0; i < digits; i++) {
        char c = p[1 + i];
        if (!tk_is_xdigit(c)) return false;
        cp <<= 4;
        if ('0' <= c && c <= '9') cp |= (uint32_t)(c - '0');
        else if ('a' <= c && c <= 'f') cp |= (uint32_t)(c - 'a' + 10);
        else cp |= (uint32_t)(c - 'A' + 10);
      }
      if (!tk_is_valid_ucn_codepoint(cp)) return false;
      *pp = p + 1 + digits;
      return true;
    }
    default:
      return false;
  }
}

static bool tolerant_string_literal_is_safe(char *p) {
  int str_prefix = 0;
  tk_string_prefix_kind_t str_prefix_kind = TK_STR_PREFIX_NONE;
  tk_char_width_t str_char_width = TK_CHAR_WIDTH_CHAR;
  bool is_string_lit = (*p == '"');
  tk_parse_string_prefix(p, &str_prefix, &str_prefix_kind, &str_char_width);
  if (str_prefix > 0) is_string_lit = true;
  if (!is_string_lit) return false;
  if (*p == '"') str_prefix = 0;
  p += str_prefix + 1;
  while (*p && *p != '\n') {
    if (*p == '"') return true;
    if (*p == '\\') {
      p++;
      if (!tolerant_skip_escape(&p)) return false;
      continue;
    }
    p++;
  }
  return false;
}

static bool tolerant_char_literal_is_safe(char *p) {
  int chr_prefix = 0;
  tk_char_prefix_kind_t chr_prefix_kind = TK_CHAR_PREFIX_NONE;
  tk_char_width_t chr_char_width = TK_CHAR_WIDTH_CHAR;
  bool is_char_lit = (*p == '\'');
  tk_parse_char_prefix(p, &chr_prefix, &chr_prefix_kind, &chr_char_width);
  if (chr_prefix > 0) is_char_lit = true;
  if (!is_char_lit) return false;
  p += chr_prefix + 1;
  int nchar = 0;
  while (*p && *p != '\n') {
    if (*p == '\'') return nchar > 0;
    if (*p == '\\') {
      p++;
      if (!tolerant_skip_escape(&p)) return false;
    } else {
      p++;
    }
    nchar++;
  }
  return false;
}

static bool tolerant_int_suffix_is_safe(char **pp) {
  char *p = *pp;
  bool seen_u = false;
  int long_cnt = 0;
  for (;;) {
    if (*p == 'u' || *p == 'U') {
      if (seen_u) return false;
      seen_u = true;
      p++;
      continue;
    }
    if (*p == 'l' || *p == 'L') {
      if (p[1] == 'l' || p[1] == 'L') {
        if (long_cnt != 0) return false;
        long_cnt = 2;
        p += 2;
      } else {
        if (long_cnt != 0) return false;
        long_cnt = 1;
        p++;
      }
      continue;
    }
    break;
  }
  if (tk_is_ident_continue_byte(*p) || *p == '.') return false;
  *pp = p;
  return true;
}

static bool tolerant_decimal_float_suffix_is_safe(char **pp) {
  char *p = *pp;
  if (*p == 'f' || *p == 'F' || *p == 'l' || *p == 'L') p++;
  if (tk_is_ident_continue_byte(*p) || *p == '.') return false;
  *pp = p;
  return true;
}

static bool tolerant_number_literal_is_safe(char *p) {
  if (!(tk_is_digit(*p) || (*p == '.' && tk_is_digit(p[1])))) return false;
  char *q = p;
  int digits = 0;

  if (*q == '0' && (q[1] == 'x' || q[1] == 'X')) {
    q += 2;
    int hex_digits = 0;
    while (tk_is_xdigit(*q)) {
      q++;
      hex_digits++;
      digits++;
    }
    if (*q == '.') {
      q++;
      while (tk_is_xdigit(*q)) {
        q++;
        hex_digits++;
        digits++;
      }
    }
    if (hex_digits == 0 || digits > 16) return false;
    if (*q == 'p' || *q == 'P') {
      q++;
      if (*q == '+' || *q == '-') q++;
      if (!tk_is_digit(*q)) return false;
      while (tk_is_digit(*q)) q++;
      return tolerant_decimal_float_suffix_is_safe(&q);
    }
    return tolerant_int_suffix_is_safe(&q);
  }

  bool is_float = (*q == '.');
  while (tk_is_digit(*q)) {
    q++;
    digits++;
  }
  if (*q == '.') {
    is_float = true;
    q++;
    while (tk_is_digit(*q)) q++;
  }
  if (*q == 'e' || *q == 'E') {
    is_float = true;
    q++;
    if (*q == '+' || *q == '-') q++;
    if (!tk_is_digit(*q)) return false;
    while (tk_is_digit(*q)) q++;
  }
  if (digits == 0 || digits > 18) return false;
  if (is_float) return tolerant_decimal_float_suffix_is_safe(&q);
  if (p[0] == '0' && tk_is_digit(p[1])) {
    for (char *r = p + 1; r < q; r++) {
      if (*r == '8' || *r == '9') return false;
    }
  }
  return tolerant_int_suffix_is_safe(&q);
}

static token_t *tokenize_one_tolerant_wasm(char **pp, token_t **cur_io, int line_no, bool at_bol, bool has_space) {
  char *p = *pp;
  int str_prefix = 0;
  tk_string_prefix_kind_t str_prefix_kind = TK_STR_PREFIX_NONE;
  tk_char_width_t str_char_width = TK_CHAR_WIDTH_CHAR;
  tk_parse_string_prefix(p, &str_prefix, &str_prefix_kind, &str_char_width);
  if (*p == '"' || str_prefix > 0) {
    if (tolerant_string_literal_is_safe(p) &&
        tokenize_string_literal(pp, cur_io, line_no, at_bol, has_space)) {
      return *cur_io;
    }
    *pp = p + 1;
    return new_token_simple(TK_UNKNOWN, *cur_io, line_no, at_bol, has_space, p, 1);
  }

  int chr_prefix = 0;
  tk_char_prefix_kind_t chr_prefix_kind = TK_CHAR_PREFIX_NONE;
  tk_char_width_t chr_char_width = TK_CHAR_WIDTH_CHAR;
  tk_parse_char_prefix(p, &chr_prefix, &chr_prefix_kind, &chr_char_width);
  if (*p == '\'' || chr_prefix > 0) {
    if (tolerant_char_literal_is_safe(p) &&
        tokenize_char_literal(pp, cur_io, line_no, at_bol, has_space)) {
      return *cur_io;
    }
    *pp = p + 1;
    return new_token_simple(TK_UNKNOWN, *cur_io, line_no, at_bol, has_space, p, 1);
  }

  if (tk_is_digit(*p) || (*p == '.' && tk_is_digit(p[1]))) {
    if (tolerant_number_literal_is_safe(p) &&
        tokenize_number_literal(pp, cur_io, line_no, at_bol, has_space)) {
      return *cur_io;
    }
    *pp = p + 1;
    return new_token_simple(TK_UNKNOWN, *cur_io, line_no, at_bol, has_space, p, 1);
  }

  token_t *tok = tokenize_one(pp, cur_io, line_no, at_bol, has_space);
  if (tok) return tok;
  *pp = p + 1;
  return new_token_simple(TK_UNKNOWN, *cur_io, line_no, at_bol, has_space, p, 1);
}
#endif

token_t *tk_stream_next(tk_token_stream_t *s) {
  if (s->done) return NULL;
  token_t head;
  head.next = NULL;
  token_t *cur = &head;
  for (;;) {
    s->p = tk_skip_ignored(s->p, &s->at_bol, &s->has_space, &s->line_no);
    if (!*s->p) {
      new_token_simple(TK_EOF, cur, s->line_no, false, false, s->p, 0);
      s->done = true;
      return head.next;
    }
    bool flag_at_bol = s->at_bol;
    bool flag_has_space = s->has_space;
    s->at_bol = false;
    s->has_space = false;
    tokenizer_context_t *ctx = s->session.ctx;
    if (!ctx->tolerate_untokenizable) {
      char *p = s->p;
      token_t *tok = tokenize_one(&p, &cur, s->line_no, flag_at_bol, flag_has_space);
      s->p = p;
      if (tok) return tok;
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_TOKENIZE_FAILED, s->p, "%s",
                  diag_message_for(DIAG_ERR_TOKENIZER_TOKENIZE_FAILED));
    }
    /* 寛容モード: scanner のエラーを longjmp で受け、トークン先頭の 1 文字を TK_UNKNOWN
     * にして進める (volatile は longjmp 後も値を保つため)。 */
#if defined(AGC_TARGET_WASM32) || defined(__wasm32__)
    return tokenize_one_tolerant_wasm(&s->p, &cur, s->line_no, flag_at_bol, flag_has_space);
#else
    char *volatile tok_start = s->p;
    volatile bool v_at_bol = flag_at_bol;
    volatile bool v_has_space = flag_has_space;
    jmp_buf tolerate_jump;
    void *previous_jump_target = ctx->tolerate_jump_target;
    ctx->tolerate_jump_target = &tolerate_jump;
    if (setjmp(tolerate_jump) != 0) {
      ctx->tolerate_jump_target = previous_jump_target;
      s->p = tok_start + 1;
      new_token_simple(TK_UNKNOWN, cur, s->line_no, v_at_bol, v_has_space, tok_start, 1);
      return head.next;
    }
    char *p = s->p;
    token_t *tok = tokenize_one(&p, &cur, s->line_no, flag_at_bol, flag_has_space);
    s->p = p;
    ctx->tolerate_jump_target = previous_jump_target;
    if (tok) return tok;
    /* tokenize_one が false (トークナイズ不能文字): 1 文字を TK_UNKNOWN に。 */
    s->p = tok_start + 1;
    new_token_simple(TK_UNKNOWN, cur, s->line_no, v_at_bol, v_has_space, tok_start, 1);
    return head.next;
#endif
  }
}

void tk_stream_close(tk_token_stream_t *s) {
  end_tokenize_session(&s->session, tk_get_current_token());
}

/* ヒープ確保版 (構造体を不透明に保ったまま埋め込み利用したい呼び出し側用)。 */
tk_token_stream_t *tk_stream_new(tokenizer_context_t *ctx, const char *in) {
  tk_token_stream_t *s = calloc(1, sizeof(*s));
  tk_stream_open(s, ctx, in);
  return s;
}

void tk_stream_delete(tk_token_stream_t *s) {
  if (!s) return;
  tk_stream_close(s);
  free(s);
}

token_t *tk_tokenize_ctx(tokenizer_context_t *ctx, const char *in) {
  tk_token_stream_t s;
  tk_stream_open(&s, ctx, in);
  token_t head;
  head.next = NULL;
  token_t *cur = &head;
  for (token_t *t; (t = tk_stream_next(&s)) != NULL; ) {
    cur->next = t;
    cur = t;
  }
  return end_tokenize_session(&s.session, head.next);
}
