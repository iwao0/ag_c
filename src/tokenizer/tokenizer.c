#include "allocator.h"
#include "tokenizer.h"
#include "charclass.h"
#include "keywords.h"
#include "literals.h"
#include "punctuator.h"
#include "scanner.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// 入力プログラム（エラーメッセージ表示用）
static char *user_input;

// 現在着目しているトークン
token_t *token;

char *get_user_input(void) {
  return user_input;
}

void set_user_input(char *p) {
  user_input = p;
}

static char *current_filename;
static tokenizer_stats_t tok_stats = {0};
static size_t stats_base_chunks = 0;
static size_t stats_base_reserved_bytes = 0;

char *get_filename(void) {
  return current_filename;
}

void set_filename(char *name) {
  current_filename = name;
}

void reset_tokenizer_stats(void) {
  stats_base_chunks = tk_allocator_total_chunks();
  stats_base_reserved_bytes = tk_allocator_total_reserved_bytes();
  tok_stats.alloc_count = 0;
  tok_stats.alloc_bytes = 0;
  tok_stats.peak_alloc_bytes = 0;
}

tokenizer_stats_t get_tokenizer_stats(void) {
  tok_stats.alloc_count = tk_allocator_total_chunks() - stats_base_chunks;
  tok_stats.alloc_bytes = tk_allocator_total_reserved_bytes() - stats_base_reserved_bytes;
  tok_stats.peak_alloc_bytes = tok_stats.alloc_bytes;
  return tok_stats;
}

static void *tcalloc(size_t n, size_t size) {
  return tk_allocator_calloc(n, size);
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
  if (!get_enable_trigraphs()) return (char *)in;
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

// エラー箇所を視覚的に表示する
void error_at(char *loc, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  int pos = loc - user_input;
  fprintf(stderr, "%s\n", user_input);
  fprintf(stderr, "%*s", pos, ""); // pos個の空白を出力
  fprintf(stderr, "^ ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

void error_tok(token_t *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  if (tok && tok->file_name) {
    fprintf(stderr, "%s:%d: ", tok->file_name, tok->line_no);
  }
  vfprintf(stderr, fmt, ap);
  if (tok) {
    if (tok->kind == TK_IDENT) {
      token_ident_t *id = (token_ident_t *)tok;
      fprintf(stderr, " (actual: '%.*s')", id->len, id->str);
    } else if (tok->kind == TK_STRING) {
      token_string_t *st = (token_string_t *)tok;
      fprintf(stderr, " (actual: '%.*s')", st->len, st->str);
    } else if (tok->kind == TK_NUM) {
      token_num_t *num = (token_num_t *)tok;
      fprintf(stderr, " (actual: '%.*s')", num->len, num->str);
    } else {
      int len = 0;
      const char *s = token_kind_str(tok->kind, &len);
      if (s && len > 0) {
        fprintf(stderr, " (actual: '%.*s')", len, s);
      }
    }
  }
  fprintf(stderr, "\n");
  exit(1);
}

const char *token_kind_str(token_kind_t kind, int *len) {
  const char *s = NULL;
  switch (kind) {
    case TK_IF: s = "if"; break;
    case TK_ELSE: s = "else"; break;
    case TK_WHILE: s = "while"; break;
    case TK_FOR: s = "for"; break;
    case TK_RETURN: s = "return"; break;
    case TK_AUTO: s = "auto"; break;
    case TK_BREAK: s = "break"; break;
    case TK_CASE: s = "case"; break;
    case TK_CONST: s = "const"; break;
    case TK_CONTINUE: s = "continue"; break;
    case TK_DEFAULT: s = "default"; break;
    case TK_DO: s = "do"; break;
    case TK_ENUM: s = "enum"; break;
    case TK_EXTERN: s = "extern"; break;
    case TK_GOTO: s = "goto"; break;
    case TK_INLINE: s = "inline"; break;
    case TK_INT: s = "int"; break;
    case TK_REGISTER: s = "register"; break;
    case TK_RESTRICT: s = "restrict"; break;
    case TK_SIGNED: s = "signed"; break;
    case TK_SIZEOF: s = "sizeof"; break;
    case TK_STATIC: s = "static"; break;
    case TK_STRUCT: s = "struct"; break;
    case TK_SWITCH: s = "switch"; break;
    case TK_TYPEDEF: s = "typedef"; break;
    case TK_UNION: s = "union"; break;
    case TK_UNSIGNED: s = "unsigned"; break;
    case TK_VOLATILE: s = "volatile"; break;
    case TK_CHAR: s = "char"; break;
    case TK_VOID: s = "void"; break;
    case TK_SHORT: s = "short"; break;
    case TK_LONG: s = "long"; break;
    case TK_FLOAT: s = "float"; break;
    case TK_DOUBLE: s = "double"; break;
    case TK_ALIGNAS: s = "_Alignas"; break;
    case TK_ALIGNOF: s = "_Alignof"; break;
    case TK_ATOMIC: s = "_Atomic"; break;
    case TK_BOOL: s = "_Bool"; break;
    case TK_COMPLEX: s = "_Complex"; break;
    case TK_GENERIC: s = "_Generic"; break;
    case TK_IMAGINARY: s = "_Imaginary"; break;
    case TK_NORETURN: s = "_Noreturn"; break;
    case TK_STATIC_ASSERT: s = "_Static_assert"; break;
    case TK_THREAD_LOCAL: s = "_Thread_local"; break;
    case TK_LPAREN: s = "("; break;
    case TK_RPAREN: s = ")"; break;
    case TK_LBRACE: s = "{"; break;
    case TK_RBRACE: s = "}"; break;
    case TK_LBRACKET: s = "["; break;
    case TK_RBRACKET: s = "]"; break;
    case TK_COMMA: s = ","; break;
    case TK_SEMI: s = ";"; break;
    case TK_ASSIGN: s = "="; break;
    case TK_PLUS: s = "+"; break;
    case TK_MINUS: s = "-"; break;
    case TK_MUL: s = "*"; break;
    case TK_DIV: s = "/"; break;
    case TK_MOD: s = "%"; break;
    case TK_BANG: s = "!"; break;
    case TK_TILDE: s = "~"; break;
    case TK_LT: s = "<"; break;
    case TK_LE: s = "<="; break;
    case TK_GT: s = ">"; break;
    case TK_GE: s = ">="; break;
    case TK_EQEQ: s = "=="; break;
    case TK_NEQ: s = "!="; break;
    case TK_ANDAND: s = "&&"; break;
    case TK_OROR: s = "||"; break;
    case TK_AMP: s = "&"; break;
    case TK_PIPE: s = "|"; break;
    case TK_CARET: s = "^"; break;
    case TK_QUESTION: s = "?"; break;
    case TK_COLON: s = ":"; break;
    case TK_INC: s = "++"; break;
    case TK_DEC: s = "--"; break;
    case TK_SHL: s = "<<"; break;
    case TK_SHR: s = ">>"; break;
    case TK_ARROW: s = "->"; break;
    case TK_PLUSEQ: s = "+="; break;
    case TK_MINUSEQ: s = "-="; break;
    case TK_MULEQ: s = "*="; break;
    case TK_DIVEQ: s = "/="; break;
    case TK_MODEQ: s = "%="; break;
    case TK_SHLEQ: s = "<<="; break;
    case TK_SHREQ: s = ">>="; break;
    case TK_ANDEQ: s = "&="; break;
    case TK_XOREQ: s = "^="; break;
    case TK_OREQ: s = "|="; break;
    case TK_ELLIPSIS: s = "..."; break;
    case TK_HASH: s = "#"; break;
    case TK_HASHHASH: s = "##"; break;
    case TK_DOT: s = "."; break;
    case TK_EOF: s = "EOF"; break;
    default: s = NULL; break;
  }
  if (len) *len = s ? (int)strlen(s) : 0;
  return s;
}

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

bool consume(char op) {
  token_kind_t kind = kind_for_char(op);
  if (kind == TK_EOF || token->kind != kind)
    return false;
  token = token->next;
  return true;
}

bool consume_str(char *op) {
  token_kind_t kind = punctuator_kind_for_str(op);
  if (kind == TK_EOF || token->kind != kind)
    return false;
  token = token->next;
  return true;
}

token_ident_t *consume_ident(void) {
  if (token->kind != TK_IDENT)
    return NULL;
  token_ident_t *tok = (token_ident_t *)token;
  token = token->next;
  return tok;
}

void expect(char op) {
  token_kind_t kind = kind_for_char(op);
  if (kind == TK_EOF || token->kind != kind) {
    error_tok(token, "'%c'ではありません", op);
  }
  token = token->next;
}

int expect_number(void) {
  if (token->kind != TK_NUM) {
    error_tok(token, "数ではありません");
  }
  long long n = ((token_num_t *)token)->val;
  if (n < INT_MIN || n > INT_MAX) {
    error_tok(token, "数値が int 範囲外です");
  }
  int val = (int)n;
  token = token->next;
  return val;
}

bool at_eof(void) { return token->kind == TK_EOF; }

// 新しいトークンを作成して、curに繋げる
static void init_token_base(token_t *tok, token_kind_t kind, int line_no) {
  tok->kind = kind;
  tok->file_name = current_filename;
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

static token_num_t *new_token_num(token_t *cur, char *str, int len, int line_no, bool at_bol, bool has_space) {
  token_num_t *tok = tcalloc(1, sizeof(token_num_t));
  init_token_base(&tok->pp.base, TK_NUM, line_no);
  tok->pp.base.at_bol = at_bol;
  tok->pp.base.has_space = has_space;
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static void choose_int_type(token_num_t *num, unsigned long long val, bool is_decimal, bool has_u, int long_cnt) {
  if (!has_u && long_cnt == 0) {
    if (is_decimal) {
      if (val <= (unsigned long long)INT_MAX) { num->is_unsigned = false; num->int_size = 0; return; }
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = 1; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = 2; return; }
    } else {
      if (val <= (unsigned long long)INT_MAX) { num->is_unsigned = false; num->int_size = 0; return; }
      if (val <= (unsigned long long)UINT_MAX) { num->is_unsigned = true; num->int_size = 0; return; }
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = 1; return; }
      if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = 1; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = 2; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = 2; return; }
    }
    error_at(num->str, "整数リテラルが大きすぎます");
  }

  if (has_u && long_cnt == 0) {
    if (val <= (unsigned long long)UINT_MAX) { num->is_unsigned = true; num->int_size = 0; return; }
    if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = 1; return; }
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = 2; return; }
    error_at(num->str, "整数リテラルが大きすぎます");
  }

  if (!has_u && long_cnt == 1) {
    if (is_decimal) {
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = 1; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = 2; return; }
    } else {
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = 1; return; }
      if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = 1; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = 2; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = 2; return; }
    }
    error_at(num->str, "整数リテラルが大きすぎます");
  }

  if (has_u && long_cnt == 1) {
    if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = 1; return; }
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = 2; return; }
    error_at(num->str, "整数リテラルが大きすぎます");
  }

  if (!has_u && long_cnt == 2) {
    if (is_decimal) {
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = 2; return; }
    } else {
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = 2; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = 2; return; }
    }
    error_at(num->str, "整数リテラルが大きすぎます");
  }

  if (has_u && long_cnt == 2) {
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = 2; return; }
    error_at(num->str, "整数リテラルが大きすぎます");
  }
}

static void parse_int_suffix(token_num_t *num, char **pp, unsigned long long val, bool is_decimal) {
  char *p = *pp;
  bool seen_u = false;
  int long_cnt = 0;

  while (true) {
    if (*p == 'u' || *p == 'U') {
      if (seen_u) error_at(p, "整数サフィックスが不正です");
      seen_u = true;
      p++;
      continue;
    }
    if (*p == 'l' || *p == 'L') {
      if ((p[1] == 'l' || p[1] == 'L')) {
        if (long_cnt == 2) error_at(p, "整数サフィックスが不正です");
        long_cnt = 2;
        p += 2;
      } else {
        if (long_cnt == 2) error_at(p, "整数サフィックスが不正です");
        long_cnt = 1;
        p++;
      }
      continue;
    }
    break;
  }

  if (tk_is_ident_start_byte(*p)) error_at(p, "整数サフィックスが不正です");

  choose_int_type(num, val, is_decimal, seen_u, long_cnt);
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
      error_at(*pp, "整数リテラルが大きすぎます");
    val = val * (unsigned long long)base + (unsigned long long)digit;
    p++;
  }
  if (!has_digit) error_at(*pp, "整数リテラルが不正です");
  *pp = p;
  return val;
}

static long long token_signed_from_u64(unsigned long long uval) {
  if (uval <= (unsigned long long)LLONG_MAX) return (long long)uval;
  return (long long)(uval & (unsigned long long)LLONG_MAX);
}

static void parse_number_literal(char **pp, token_num_t *num) {
  char *p = *pp;

  // 16進数/2進数/8進数/10進数
  if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
    // 16進数 (整数 or 浮動小数点)
    bool is_hex_float = false;
    for (char *q = p + 2; tk_is_xdigit(*q) || *q == '.' || *q == 'p' || *q == 'P'; q++) {
      if (*q == '.' || *q == 'p' || *q == 'P') {
        is_hex_float = true;
        break;
      }
    }

    if (is_hex_float) {
      char *end;
      num->fval = strtod(p, &end);
      if (end == p) error_at(p, "16進浮動小数点リテラルが不正です");
      if (*end == 'f' || *end == 'F') {
        num->is_float = 1;
        end++;
      } else if (*end == 'l' || *end == 'L') {
        num->is_float = 2;
        end++;
      } else {
        num->is_float = 2;
      }
      if (tk_is_ident_start_byte(*end)) error_at(end, "浮動小数点サフィックスが不正です");
      p = end;
    } else {
      p += 2;
      unsigned long long val = parse_digits(&p, 16);
      num->uval = val;
      num->val = token_signed_from_u64(val);
      num->is_float = 0;
      num->int_base = 16;
      parse_int_suffix(num, &p, val, false);
    }
  } else if (*p == '0' && (p[1] == 'b' || p[1] == 'B')) {
    if (get_strict_c11_mode() || !get_enable_binary_literals()) {
      error_at(p, "2進数リテラルは strict C11 では未対応です");
    }
    p += 2;
    if (*p != '0' && *p != '1') error_at(p, "2進数リテラルが不正です");
    unsigned long long val = parse_digits(&p, 2);
    num->uval = val;
    num->val = token_signed_from_u64(val);
    num->is_float = 0;
    num->int_base = 2;
    parse_int_suffix(num, &p, val, false);
  } else if (*p == '0' && tk_is_digit(p[1])) {
    if (p[1] == '8' || p[1] == '9') error_at(p, "8進数リテラルが不正です");
    p++;
    unsigned long long val = 0;
    if (*p >= '0' && *p <= '7') {
      p--;
      val = parse_digits(&p, 8);
    }
    num->uval = val;
    num->val = token_signed_from_u64(val);
    num->is_float = 0;
    num->int_base = 8;
    parse_int_suffix(num, &p, val, false);
  } else {
    // 10進整数はまず数字列だけ高速スキャンし、浮動小数点判定を早期化する
    bool is_float = false;
    if (*p == '.') {
      is_float = true;
    } else {
      char *q = p;
      while (tk_is_digit(*q)) q++;
      if (*q == '.' || *q == 'e' || *q == 'E')
        is_float = true;
    }

    if (is_float) {
      char *end;
      num->fval = strtod(p, &end);
      if (*end == 'f' || *end == 'F') {
        num->is_float = 1; // float
        end++;
      } else if (*end == 'l' || *end == 'L') {
        num->is_float = 2; // long double は未対応だが double 扱い
        end++;
      } else {
        num->is_float = 2; // デフォルトは double
      }
      if (tk_is_ident_start_byte(*end)) error_at(end, "浮動小数点サフィックスが不正です");
      p = end;
    } else {
      unsigned long long val = parse_digits(&p, 10);
      num->uval = val;
      num->val = token_signed_from_u64(val);
      num->is_float = 0;
      num->int_base = 10;
      parse_int_suffix(num, &p, val, true);
    }
  }

  if (*p == '.' || tk_is_ident_continue_byte(*p)) {
    error_at(p, "数値リテラルが不正です");
  }
  *pp = p;
}

// 文字列 p をトークナイズしてその結果へのポインタを返す
token_t *tokenize(char *p) {
  tk_allocator_set_expected_size(strlen(p));
  char *normalized = replace_trigraphs(p);
  user_input = normalized;
  p = normalized;
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
    int str_prefix_kind = 0;
    int str_char_width = 1;
    bool is_string_lit = false;
    switch (*p) {
      case '"':
        is_string_lit = true;
        break;
      case 'L':
        if (p[1] == '"') { is_string_lit = true; str_prefix = 1; str_prefix_kind = 1; str_char_width = 4; }
        break;
      case 'u':
        if (p[1] == '8' && p[2] == '"') { is_string_lit = true; str_prefix = 2; str_prefix_kind = 4; str_char_width = 1; }
        else if (p[1] == '"') { is_string_lit = true; str_prefix = 1; str_prefix_kind = 2; str_char_width = 2; }
        break;
      case 'U':
        if (p[1] == '"') { is_string_lit = true; str_prefix = 1; str_prefix_kind = 3; str_char_width = 4; }
        break;
    }
    if (is_string_lit) {
      if (*p == '"') {
        str_prefix = 0;
        str_prefix_kind = 0;
        str_char_width = 1;
      }
      p += str_prefix;
      p++; // 開き引用符をスキップ
      char *start = p;
      while (true) {
        if (*p == '\0' || *p == '\n') {
          error_at(p, "文字列リテラルが閉じられていません");
        }
        if (*p == '"') break;
        if (*p == '\\') {
          p++;
          tk_read_escape_char(&p);
          continue;
        }
        p++;
      }
      int len = p - start;
      p++; // 閉じ引用符をスキップ
      token_string_t *st = new_token_string(cur, start, len, line_no, _at_bol, _has_space);
      st->char_width = str_char_width;
      st->str_prefix_kind = str_prefix_kind;
      cur = (token_t *)st;
      continue;
    }

    // 文字リテラル（接頭辞 L/u/U を含む）
    int chr_prefix = 0;
    int chr_prefix_kind = 0;
    int chr_char_width = 1;
    bool is_char_lit = false;
    switch (*p) {
      case '\'':
        is_char_lit = true;
        break;
      case 'L':
        if (p[1] == '\'') { is_char_lit = true; chr_prefix = 1; chr_prefix_kind = 1; chr_char_width = 4; }
        break;
      case 'u':
        if (p[1] == '\'') { is_char_lit = true; chr_prefix = 1; chr_prefix_kind = 2; chr_char_width = 2; }
        break;
      case 'U':
        if (p[1] == '\'') { is_char_lit = true; chr_prefix = 1; chr_prefix_kind = 3; chr_char_width = 4; }
        break;
    }
    if (is_char_lit) {
      char *start = p;
      p += chr_prefix;
      p++; // 開きクォートをスキップ
      if (*p == '\0' || *p == '\n') {
        error_at(p, "文字リテラルが閉じられていません");
      }
      if (*p == '\'') {
        error_at(p, "空の文字リテラルは使えません");
      }
      unsigned long long ch = 0;
      int nchar = 0;
      if (chr_prefix_kind == 0) {
        // 通常文字定数はマルチ文字定数を許可（実装定義）
        while (*p && *p != '\'') {
          int one = 0;
          if (*p == '\\') {
            p++;
            one = tk_read_escape_char(&p);
          } else if (*p == '\n') {
            error_at(p, "文字リテラルが不正です");
          } else {
            one = (unsigned char)*p;
            p++;
          }
          ch = ((ch << 8) | (unsigned)(one & 0xFF)) & 0xFFFFFFFFULL;
          nchar++;
        }
      } else {
        // 接頭辞付き文字定数は1文字のみを扱う
        int one = 0;
        if (*p == '\\') {
          p++;
          one = tk_read_escape_char(&p);
        } else {
          one = (unsigned char)*p;
          p++;
        }
        ch = (unsigned long long)(unsigned)one;
        nchar = 1;
        if (*p != '\'') {
          error_at(p, "接頭辞付き文字リテラルは1文字のみ対応です");
        }
      }
      if (nchar == 0 || *p != '\'') {
        error_at(p, "文字リテラルが不正です");
      }
      p++; // 閉じクォートをスキップ
      int len = p - start;
      token_num_t *num = new_token_num(cur, start, len, line_no, _at_bol, _has_space);
      num->uval = ch;
      num->val = (long long)ch;
      num->is_float = 0;
      num->int_base = 10;
      num->is_unsigned = false;
      num->int_size = 0;
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
    if (tk_scan_ident_start(p, &adv)) {
      char *start = p;
      p += adv;
      while (tk_scan_ident_continue(p, &adv))
        p += adv;
      int len = p - start;
      char *id_str = start;
      int id_len = len;
      bool has_ucn = false;
      if (memchr(start, '\\', (size_t)len) != NULL) {
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
      token_num_t *num = new_token_num(cur, p, 0, line_no, _at_bol, _has_space);
      parse_number_literal(&p, num);
      num->len = p - start;
      num->str = start;
      cur = (token_t *)num;
      continue;
    }

    error_at(p, "トークナイズできません");
  }

  new_token_simple(TK_EOF, cur, line_no, false, false);
  return head.next;
}
