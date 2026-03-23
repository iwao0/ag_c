#include "internal/allocator.h"
#include "internal/branch_hint.h"
#include "tokenizer.h"
#include "../diag/diag.h"
#include "internal/charclass.h"
#include "internal/diag_helper.h"
#include "internal/keywords.h"
#include "internal/literals.h"
#include "internal/punctuator.h"
#include "internal/scanner.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static tokenizer_stats_t tok_stats = {0};
static size_t stats_base_chunks = 0;
static size_t stats_base_reserved_bytes = 0;
static size_t max_token_len_for_test = (size_t)INT_MAX;
static tokenizer_context_t *active_ctx;

static tokenizer_context_t *runtime_ctx(void) {
  return active_ctx ? active_ctx : tk_get_default_context();
}

static tokenizer_context_t *effective_ctx(tokenizer_context_t *ctx) {
  return ctx ? ctx : runtime_ctx();
}

static inline void advance_current_token(tokenizer_context_t *ctx, token_t *cur) {
  ctx->current_token = cur ? cur->next : NULL;
}

static token_t *require_current_token(tokenizer_context_t *ctx, diag_error_id_t id) {
  token_t *cur = ctx ? ctx->current_token : NULL;
  if (!cur) {
    diag_emit_tokf(id, NULL, "%s", diag_message_for(id));
  }
  return cur;
}

token_t *tk_get_current_token(void) {
  return tk_get_current_token_ctx(NULL);
}

token_t *tk_get_current_token_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  return use_ctx ? use_ctx->current_token : NULL;
}

void tk_set_current_token(token_t *tok) {
  tk_set_current_token_ctx(NULL, tok);
}

void tk_set_current_token_ctx(tokenizer_context_t *ctx, token_t *tok) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  if (use_ctx) {
    use_ctx->current_token = tok;
  }
}

const char *tk_get_user_input_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  return use_ctx ? use_ctx->user_input : NULL;
}

void tk_set_user_input_ctx(tokenizer_context_t *ctx, const char *p) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  if (use_ctx) {
    use_ctx->user_input = p;
  }
}

/** @brief Tokenizer統計の計測基準点をリセットする。 */
void tk_reset_tokenizer_stats(void) {
  stats_base_chunks = tk_allocator_total_chunks();
  stats_base_reserved_bytes = tk_allocator_total_reserved_bytes();
  tok_stats.alloc_count = 0;
  tok_stats.alloc_bytes = 0;
  tok_stats.peak_alloc_bytes = 0;
}

/** @brief Tokenizer統計（alloc回数/bytes）を取得する。 */
tokenizer_stats_t tk_get_tokenizer_stats(void) {
  tok_stats.alloc_count = tk_allocator_total_chunks() - stats_base_chunks;
  tok_stats.alloc_bytes = tk_allocator_total_reserved_bytes() - stats_base_reserved_bytes;
  tok_stats.peak_alloc_bytes = tok_stats.alloc_bytes;
  return tok_stats;
}

const char *tk_get_filename_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  return use_ctx ? use_ctx->current_filename : NULL;
}

void tk_set_filename_ctx(tokenizer_context_t *ctx, const char *name) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  if (use_ctx) {
    use_ctx->current_filename = name;
  }
}

static void *tcalloc(size_t n, size_t size) {
  return tk_allocator_calloc(n, size);
}

static int checked_span_len(char *start, char *end, const char *what) {
  ptrdiff_t diff = end - start;
  if (diff < 0 || (size_t)diff > max_token_len_for_test || (size_t)diff > (size_t)INT_MAX) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME, start, "%s", diag_message_for(DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME), (char *)what);
  }
  return (int)diff;
}

void tk_set_max_token_len_limit_for_test(size_t max_len) {
  max_token_len_for_test = (max_len == 0) ? (size_t)INT_MAX : max_len;
}

static char trigraph_to_char(char c) {
  switch (c) {
    case '=': return '#';
    case '(': return '[';
    case '/': return '\\';
    case ')': return ']';
    case '\'': return '^';
    case '<': return '{';
    case '>': return '}';
    case '!': return '|';
    case '-': return '~';
    default: return '\0';
  }
}

// 翻訳フェーズ1: trigraph を置換する
static char *replace_trigraphs(const char *in) {
  if (!tk_ctx_get_enable_trigraphs(runtime_ctx())) return (char *)in;
  size_t n = strlen(in);
  bool has_trigraph = false;
  for (size_t i = 0; i + 2 < n; i++) {
    if (in[i] == '?' && in[i + 1] == '?' && trigraph_to_char(in[i + 2])) {
      has_trigraph = true;
      break;
    }
  }
  if (!has_trigraph) return (char *)in;

  char *out = tcalloc(n + 1, 1);
  size_t i = 0;
  size_t j = 0;

  while (i < n) {
    if (i + 2 < n && in[i] == '?' && in[i + 1] == '?') {
      char mapped = trigraph_to_char(in[i + 2]);
      if (mapped) {
        out[j++] = mapped;
        i += 3;
        continue;
      }
    }
    out[j++] = in[i++];
  }
  out[j] = '\0';
  return out;
}

/** @brief 1文字演算子/区切りの token kind を返す。 */
static token_kind_t kind_for_char(char op) {
  switch (op) {
    case '(': return TK_LPAREN;
    case ')': return TK_RPAREN;
    case '{': return TK_LBRACE;
    case '}': return TK_RBRACE;
    case '[': return TK_LBRACKET;
    case ']': return TK_RBRACKET;
    case ',': return TK_COMMA;
    case ';': return TK_SEMI;
    case '=': return TK_ASSIGN;
    case '+': return TK_PLUS;
    case '-': return TK_MINUS;
    case '*': return TK_MUL;
    case '/': return TK_DIV;
    case '%': return TK_MOD;
    case '!': return TK_BANG;
    case '~': return TK_TILDE;
    case '<': return TK_LT;
    case '>': return TK_GT;
    case '&': return TK_AMP;
    case '|': return TK_PIPE;
    case '^': return TK_CARET;
    case '?': return TK_QUESTION;
    case ':': return TK_COLON;
    case '#': return TK_HASH;
    case '.': return TK_DOT;
    default: return TK_EOF;
  }
}

/** @brief 次トークンが指定1文字なら消費して true を返す。 */
bool tk_consume(char op) {
  return tk_consume_ctx(NULL, op);
}

bool tk_consume_ctx(tokenizer_context_t *ctx, char op) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  token_kind_t kind = kind_for_char(op);
  if (!cur || kind == TK_EOF || cur->kind != kind)
    return false;
  advance_current_token(use_ctx, cur);
  return true;
}

/** @brief 次トークンが指定記号文字列なら消費して true を返す。 */
bool tk_consume_str(const char *op) {
  return tk_consume_str_ctx(NULL, op);
}

bool tk_consume_str_ctx(tokenizer_context_t *ctx, const char *op) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  token_kind_t kind = punctuator_kind_for_str(op);
  if (!cur || kind == TK_EOF || cur->kind != kind)
    return false;
  advance_current_token(use_ctx, cur);
  return true;
}

/** @brief 次トークンが識別子なら消費して返す。 */
token_ident_t *tk_consume_ident(void) {
  return tk_consume_ident_ctx(NULL);
}

token_ident_t *tk_consume_ident_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  if (!cur || cur->kind != TK_IDENT)
    return NULL;
  token_ident_t *tok = (token_ident_t *)cur;
  advance_current_token(use_ctx, cur);
  return tok;
}

/** @brief 次トークンが指定1文字であることを期待して消費する。 */
void tk_expect(char op) {
  tk_expect_ctx(NULL, op);
}

void tk_expect_ctx(tokenizer_context_t *ctx, char op) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  token_t *cur = require_current_token(use_ctx, DIAG_ERR_TOKENIZER_EXPECTED_TOKEN);
  token_kind_t kind = kind_for_char(op);
  if (kind == TK_EOF || cur->kind != kind) {
    diag_emit_tokf(DIAG_ERR_TOKENIZER_EXPECTED_TOKEN, cur, "%s: '%c'",
                   diag_message_for(DIAG_ERR_TOKENIZER_EXPECTED_TOKEN), op);
  }
  advance_current_token(use_ctx, cur);
}

/** @brief 次トークンが整数であることを期待し int 値を返す。 */
int tk_expect_number(void) {
  return tk_expect_number_ctx(NULL);
}

int tk_expect_number_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  token_t *cur = require_current_token(use_ctx, DIAG_ERR_TOKENIZER_EXPECTED_INTEGER);
  if (cur->kind != TK_NUM) {
    TK_DIAG_TOK(DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, cur);
  }
  if (tk_as_num(cur)->num_kind != TK_NUM_KIND_INT) {
    TK_DIAG_TOK(DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, cur);
  }
  long long n = tk_as_num_int(cur)->val;
  if (n < INT_MIN || n > INT_MAX) {
    TK_DIAG_TOK(DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, cur);
  }
  int val = (int)n;
  advance_current_token(use_ctx, cur);
  return val;
}

/** @brief 現在トークンが EOF かを返す。 */
bool tk_at_eof(void) { return tk_at_eof_ctx(NULL); }

bool tk_at_eof_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  return cur && cur->kind == TK_EOF;
}

// 新しいトークンを作成して、curに繋げる
static void init_token_base(token_t *tok, token_kind_t kind, int line_no) {
  tok->kind = kind;
  tokenizer_context_t *ctx = runtime_ctx();
  tok->file_name_id = tk_filename_intern(ctx ? ctx->current_filename : NULL);
  tok->line_no = line_no;
}

static token_t *new_token_simple(token_kind_t kind, token_t *cur, int line_no, bool at_bol, bool has_space) {
  token_pp_t *tok = tcalloc(1, sizeof(token_pp_t));
  init_token_base(&tok->base, kind, line_no);
  tok->base.at_bol = at_bol;
  tok->base.has_space = has_space;
  cur->next = (token_t *)tok;
  return (token_t *)tok;
}

static token_ident_t *new_token_ident(token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space) {
  token_ident_t *tok = tcalloc(1, sizeof(token_ident_t));
  init_token_base(&tok->pp.base, TK_IDENT, line_no);
  tok->pp.base.at_bol = at_bol;
  tok->pp.base.has_space = has_space;
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_string_t *new_token_string(token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space) {
  token_string_t *tok = tcalloc(1, sizeof(token_string_t));
  init_token_base(&tok->pp.base, TK_STRING, line_no);
  tok->pp.base.at_bol = at_bol;
  tok->pp.base.has_space = has_space;
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

typedef struct parsed_num_t parsed_num_t;
struct parsed_num_t {
  long long val;
  unsigned long long uval;
  double fval;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
  bool is_unsigned;
  tk_int_size_t int_size;
  uint8_t int_base;
  tk_char_width_t char_width;
  tk_char_prefix_kind_t char_prefix_kind;
};

static token_num_int_t *new_token_num_int(
    token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space) {
  token_num_int_t *tok = tcalloc(1, sizeof(token_num_int_t));
  init_token_base(&tok->base.pp.base, TK_NUM, line_no);
  tok->base.pp.base.at_bol = at_bol;
  tok->base.pp.base.has_space = has_space;
  tok->base.str = str;
  tok->base.len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_num_float_t *new_token_num_float(
    token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space) {
  token_num_float_t *tok = tcalloc(1, sizeof(token_num_float_t));
  init_token_base(&tok->base.pp.base, TK_NUM, line_no);
  tok->base.pp.base.at_bol = at_bol;
  tok->base.pp.base.has_space = has_space;
  tok->base.str = str;
  tok->base.len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static void choose_int_type(
    parsed_num_t *num, unsigned long long val, bool is_decimal, bool has_u, int long_cnt, char *err_loc) {
  if (!has_u && long_cnt == 0) {
    if (is_decimal) {
      if (val <= (unsigned long long)INT_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_INT; return; }
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    } else {
      if (val <= (unsigned long long)INT_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_INT; return; }
      if (val <= (unsigned long long)UINT_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_INT; return; }
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (has_u && long_cnt == 0) {
    if (val <= (unsigned long long)UINT_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_INT; return; }
    if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (!has_u && long_cnt == 1) {
    if (is_decimal) {
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    } else {
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (has_u && long_cnt == 1) {
    if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (!has_u && long_cnt == 2) {
    if (is_decimal) {
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    } else {
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (has_u && long_cnt == 2) {
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }
}

static void parse_int_suffix(parsed_num_t *num, char **pp, unsigned long long val, bool is_decimal, char *err_loc) {
  char *p = *pp;
  bool seen_u = false;
  int long_cnt = 0;

  while (true) {
    if (*p == 'u' || *p == 'U') {
      if (seen_u) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));
      seen_u = true;
      p++;
      continue;
    }
    if (*p == 'l' || *p == 'L') {
      if ((p[1] == 'l' || p[1] == 'L')) {
        if (long_cnt == 2) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));
        long_cnt = 2;
        p += 2;
      } else {
        if (long_cnt == 2) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));
        long_cnt = 1;
        p++;
      }
      continue;
    }
    break;
  }

  if (tk_is_ident_start_byte(*p))
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));

  choose_int_type(num, val, is_decimal, seen_u, long_cnt, err_loc);
  *pp = p;
}

static unsigned long long parse_digits(char **pp, int base) {
  char *p = *pp;
  unsigned long long val = 0;
  bool has_digit = false;
  while (*p) {
    int digit;
    if ('0' <= *p && *p <= '9') digit = *p - '0';
    else if ('a' <= *p && *p <= 'f') digit = *p - 'a' + 10;
    else if ('A' <= *p && *p <= 'F') digit = *p - 'A' + 10;
    else break;
    if (digit >= base) break;
    has_digit = true;
    if (val > (ULLONG_MAX - (unsigned long long)digit) / (unsigned long long)base)
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, *pp, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
    val = val * (unsigned long long)base + (unsigned long long)digit;
    p++;
  }
  if (!has_digit) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID, *pp, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID));
  *pp = p;
  return val;
}

static long long token_signed_from_u64(unsigned long long uval);

static bool try_parse_decimal_int_fast(char **pp, parsed_num_t *num) {
  char *p = *pp;
  if (!tk_is_digit(*p)) return false;

  unsigned long long val = 0;
  while (tk_is_digit(*p)) {
    int digit = *p - '0';
    if (val > (ULLONG_MAX - (unsigned long long)digit) / 10ULL) {
      return false;
    }
    val = val * 10ULL + (unsigned long long)digit;
    p++;
  }

  if (*p == '.' || *p == 'e' || *p == 'E') return false;

  num->uval = val;
  num->val = token_signed_from_u64(val);
  num->fp_kind = TK_FLOAT_KIND_NONE;
  num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  num->int_base = 10;
  if (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L') {
    parse_int_suffix(num, &p, val, true, *pp);
  } else {
    choose_int_type(num, val, true, false, 0, *pp);
  }
  if (*p == '.' || tk_is_ident_continue_byte(*p)) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID));
  }
  *pp = p;
  return true;
}

static long long token_signed_from_u64(unsigned long long uval) {
  if (uval <= (unsigned long long)LLONG_MAX) return (long long)uval;
  return (long long)(uval & (unsigned long long)LLONG_MAX);
}

static inline bool has_decimal_float_marker(const char *p) {
  if (*p == '.') return true;
  while (tk_is_digit(*p)) p++;
  return *p == '.' || *p == 'e' || *p == 'E';
}

static inline bool has_hex_float_marker(const char *p) {
  // p points to "0x" or "0X".
  for (char *q = (char *)p + 2; *q; q++) {
    if (*q == '.' || *q == 'p' || *q == 'P') return true;
    if (!tk_is_xdigit(*q)) break;
  }
  return false;
}

static void tk_audit_extension(char *loc, diag_text_id_t text_id) {
  tokenizer_context_t *ctx = runtime_ctx();
  if (!tk_ctx_get_enable_c11_audit_extensions(ctx)) return;
  const char *input = ctx ? ctx->user_input : NULL;
  int pos = input ? (int)(loc - input) : 0;
  if (pos < 0) pos = 0;
  fprintf(stderr, "[%s] %s: %s (offset %d)\n",
          diag_text_for(DIAG_TEXT_WARNING),
          diag_text_for(DIAG_TEXT_C11_AUDIT_PREFIX),
          diag_text_for(text_id), pos);
}

static void parse_number_literal(char **pp, parsed_num_t *num) {
  char *p = *pp;

  // 16進数/2進数/8進数/10進数
  if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
    // 16進数 (整数 or 浮動小数点)
    if (has_hex_float_marker(p)) {
      char *end;
      num->fval = strtod(p, &end);
      if (end == p) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID));
      if (*end == 'f' || *end == 'F') {
        num->fp_kind = TK_FLOAT_KIND_FLOAT;
        num->float_suffix_kind = TK_FLOAT_SUFFIX_F;
        end++;
      } else if (*end == 'l' || *end == 'L') {
        num->fp_kind = TK_FLOAT_KIND_LONG_DOUBLE;
        num->float_suffix_kind = TK_FLOAT_SUFFIX_L;
        end++;
      } else {
        num->fp_kind = TK_FLOAT_KIND_DOUBLE;
        num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      }
      if (tk_is_ident_start_byte(*end))
        TK_DIAG_ATF(DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID, end, "%s", diag_message_for(DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID));
      p = end;
    } else {
      p += 2;
      unsigned long long val = parse_digits(&p, 16);
      num->uval = val;
      num->val = token_signed_from_u64(val);
      num->fp_kind = TK_FLOAT_KIND_NONE;
      num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      num->int_base = 16;
      parse_int_suffix(num, &p, val, false, *pp);
    }
  } else if (*p == '0' && (p[1] == 'b' || p[1] == 'B')) {
    if (tk_ctx_get_strict_c11_mode(runtime_ctx()) || !tk_ctx_get_enable_binary_literals(runtime_ctx())) {
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED));
    }
    tk_audit_extension(p, DIAG_TEXT_C11_AUDIT_BINARY_LITERAL_EXTENSION);
    p += 2;
    if (*p != '0' && *p != '1')
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID));
    unsigned long long val = parse_digits(&p, 2);
    num->uval = val;
    num->val = token_signed_from_u64(val);
    num->fp_kind = TK_FLOAT_KIND_NONE;
    num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
    num->int_base = 2;
    parse_int_suffix(num, &p, val, false, *pp);
  } else if (*p == '0' && tk_is_digit(p[1])) {
    if (p[1] == '8' || p[1] == '9')
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID));
    p++;
    unsigned long long val = 0;
    if (*p >= '0' && *p <= '7') {
      p--;
      val = parse_digits(&p, 8);
    }
    num->uval = val;
    num->val = token_signed_from_u64(val);
    num->fp_kind = TK_FLOAT_KIND_NONE;
    num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
    num->int_base = 8;
    parse_int_suffix(num, &p, val, false, *pp);
  } else {
    if (try_parse_decimal_int_fast(&p, num)) {
      *pp = p;
      return;
    }

    if (has_decimal_float_marker(p)) {
      char *end;
      num->fval = strtod(p, &end);
      if (*end == 'f' || *end == 'F') {
        num->fp_kind = TK_FLOAT_KIND_FLOAT;
        num->float_suffix_kind = TK_FLOAT_SUFFIX_F;
        end++;
      } else if (*end == 'l' || *end == 'L') {
        num->fp_kind = TK_FLOAT_KIND_LONG_DOUBLE; // codegenでは現状double経路へlowering
        num->float_suffix_kind = TK_FLOAT_SUFFIX_L;
        end++;
      } else {
        num->fp_kind = TK_FLOAT_KIND_DOUBLE;
        num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      }
      if (tk_is_ident_start_byte(*end))
        TK_DIAG_ATF(DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID, end, "%s", diag_message_for(DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID));
      p = end;
    } else {
      unsigned long long val = parse_digits(&p, 10);
      num->uval = val;
      num->val = token_signed_from_u64(val);
      num->fp_kind = TK_FLOAT_KIND_NONE;
      num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      num->int_base = 10;
      parse_int_suffix(num, &p, val, true, *pp);
    }
  }

  if (*p == '.' || tk_is_ident_continue_byte(*p)) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID));
  }
  *pp = p;
}

/** @brief 入力文字列をトークナイズし、先頭トークンを返す。 */
token_t *tk_tokenize(const char *p) {
  return tk_tokenize_ctx(tk_get_default_context(), p);
}

token_t *tk_tokenize_ctx(tokenizer_context_t *ctx, const char *in) {
  tokenizer_context_t *prev_ctx = active_ctx;
  active_ctx = ctx ? ctx : tk_get_default_context();
  tk_set_current_token_ctx(active_ctx, NULL);
  tk_allocator_set_expected_size(strlen(in));
  char *normalized = replace_trigraphs(in);
  tk_set_user_input_ctx(active_ctx, normalized);
  char *p = normalized;
  token_t head;
  head.next = NULL;
  token_t *cur = &head;
  
  bool at_bol = true;
  bool has_space = false;
  int line_no = 1;

  while (*p) {
    p = tk_skip_ignored(p, &at_bol, &has_space, &line_no);
    if (!*p) break;

    // 新しいトークンの処理前にフラグを覚えておく
    bool _at_bol = at_bol;
    bool _has_space = has_space;
    at_bol = false;
    has_space = false;

    // 複数文字の演算子・記号（最長一致）
    token_kind_t matched_kind = TK_EOF;
    int matched_len = 0;
    if (match_punctuator(p, &matched_kind, &matched_len) && matched_len >= 2) {
      cur = new_token_simple(matched_kind, cur, line_no, _at_bol, _has_space);
      p += matched_len;
      continue;
    }
    // 文字列リテラル（接頭辞 L/u/U/u8 を含む）
    int str_prefix = 0;
    tk_string_prefix_kind_t str_prefix_kind = TK_STR_PREFIX_NONE;
    tk_char_width_t str_char_width = TK_CHAR_WIDTH_CHAR;
    bool is_string_lit = (*p == '"');
    tk_parse_string_prefix(p, &str_prefix, &str_prefix_kind, &str_char_width);
    if (str_prefix > 0) {
      is_string_lit = true;
    }
    if (is_string_lit) {
      if (*p == '"') {
        str_prefix = 0;
        str_prefix_kind = TK_STR_PREFIX_NONE;
        str_char_width = TK_CHAR_WIDTH_CHAR;
      }
      p += str_prefix;
      p++; // 開き引用符をスキップ
      char *start = p;
      while (true) {
        if (*p == '\0' || *p == '\n') {
          TK_DIAG_ATF(DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED));
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
      p++; // 閉じ引用符をスキップ
      token_string_t *st = new_token_string(cur, start, len, line_no, _at_bol, _has_space);
      st->char_width = str_char_width;
      st->str_prefix_kind = str_prefix_kind;
      cur = (token_t *)st;
      continue;
    }

    // 文字リテラル（接頭辞 L/u/U を含む）
    int chr_prefix = 0;
    tk_char_prefix_kind_t chr_prefix_kind = TK_CHAR_PREFIX_NONE;
    tk_char_width_t chr_char_width = TK_CHAR_WIDTH_CHAR;
    bool is_char_lit = (*p == '\'');
    tk_parse_char_prefix(p, &chr_prefix, &chr_prefix_kind, &chr_char_width);
    if (chr_prefix > 0) {
      is_char_lit = true;
    }
    if (is_char_lit) {
      char *start = p;
      p += chr_prefix;
      p++; // 開きクォートをスキップ
      if (*p == '\0' || *p == '\n') {
        TK_DIAG_ATF(DIAG_ERR_TOKENIZER_UNTERMINATED_LITERAL, p,
                       "文字リテラルが閉じられていません");
      }
      if (*p == '\'') {
        TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY));
      }
      unsigned long long ch = 0;
      int nchar = 0;
      // 通常/接頭辞付きのいずれも、複数文字定数は実装定義として受理する。
      while (*p && *p != '\'') {
        int one = 0;
        if (*p == '\\') {
          p++;
          one = tk_read_escape_char(&p);
        } else if (*p == '\n') {
          TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID));
        } else {
          one = (unsigned char)*p;
          p++;
        }
        ch = ((ch << 8) | (unsigned)(one & 0xFF)) & 0xFFFFFFFFULL;
        nchar++;
      }
      if (nchar == 0 || *p != '\'') {
        TK_DIAG_ATF(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID));
      }
      p++; // 閉じクォートをスキップ
      int len = checked_span_len(start, p, "文字リテラル");
      token_num_int_t *num = new_token_num_int(cur, start, len, line_no, _at_bol, _has_space);
      num->base.num_kind = TK_NUM_KIND_INT;
      num->uval = ch;
      num->val = (long long)ch;
      num->int_base = 10;
      num->is_unsigned = false;
      num->int_size = TK_INT_SIZE_INT;
      num->char_width = chr_char_width;
      num->char_prefix_kind = chr_prefix_kind;
      cur = (token_t *)num;
      continue;
    }

    // 1文字の記号 (+, -, *, /, %, (, ), <, >, ;, =, {, }, ,, &, [, ], #, ., !, ~, |, ^, ?, :)
    if (tk_is_punctuator1(*p) || (*p == '.' && !tk_is_digit(p[1]))) {
      token_kind_t kind = kind_for_char(*p);
      cur = new_token_simple(kind, cur, line_no, _at_bol, _has_space);
      p++;
      continue;
    }

    // 識別子またはキーワード (a〜z で始まる連続した英字)
    // 識別子・キーワード（英字または_で始まり、英数字または_が続く）
    int adv = 0;
    if (TK_LIKELY(tk_scan_ident_start(p, &adv))) {
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
        cur = new_token_simple(kw_kind, cur, line_no, _at_bol, _has_space);
      } else {
        token_ident_t *id = new_token_ident(cur, id_str, id_len, line_no, _at_bol, _has_space);
        cur = (token_t *)id;
      }
      continue;
    }

    // 数値リテラル (整数 または 浮動小数点数)
    if (tk_is_digit(*p) || (*p == '.' && tk_is_digit(p[1]))) {
      char *start = p; // Keep track of the start of the number for length calculation
      parsed_num_t parsed = {0};
      parse_number_literal(&p, &parsed);
      int len = checked_span_len(start, p, "数値リテラル");
      if (parsed.fp_kind == TK_FLOAT_KIND_NONE) {
        token_num_int_t *num = new_token_num_int(cur, start, len, line_no, _at_bol, _has_space);
        num->base.num_kind = TK_NUM_KIND_INT;
        num->val = parsed.val;
        num->uval = parsed.uval;
        num->is_unsigned = parsed.is_unsigned;
        num->int_size = parsed.int_size;
        num->int_base = parsed.int_base;
        num->char_width = parsed.char_width;
        num->char_prefix_kind = parsed.char_prefix_kind;
        cur = (token_t *)num;
      } else {
        token_num_float_t *num = new_token_num_float(cur, start, len, line_no, _at_bol, _has_space);
        num->base.num_kind = TK_NUM_KIND_FLOAT;
        num->fval = parsed.fval;
        num->fp_kind = parsed.fp_kind;
        num->float_suffix_kind = parsed.float_suffix_kind;
        cur = (token_t *)num;
      }
      continue;
    }

    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_TOKENIZE_FAILED, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_TOKENIZE_FAILED));
  }

  new_token_simple(TK_EOF, cur, line_no, false, false);
  tk_set_current_token(head.next);
  active_ctx = prev_ctx;
  return head.next;
}
