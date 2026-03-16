#include "tokenizer.h"
#include <ctype.h>
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
static bool strict_c11_mode = false;

char *get_filename(void) {
  return current_filename;
}

void set_filename(char *name) {
  current_filename = name;
}

bool get_strict_c11_mode(void) {
  return strict_c11_mode;
}

void set_strict_c11_mode(bool strict) {
  strict_c11_mode = strict;
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
  size_t n = strlen(in);
  char *out = calloc(n + 1, 1);
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

static bool starts_with_ucn(const char *p, int *len) {
  if (p[0] != '\\' || (p[1] != 'u' && p[1] != 'U')) return false;
  int digits = (p[1] == 'u') ? 4 : 8;
  for (int i = 0; i < digits; i++) {
    if (!isxdigit((unsigned char)p[2 + i])) return false;
  }
  *len = 2 + digits;
  return true;
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

static token_kind_t kind_for_op(const char *op) {
  if (strcmp(op, "==") == 0) return TK_EQEQ;
  if (strcmp(op, "!=") == 0) return TK_NEQ;
  if (strcmp(op, "<=") == 0) return TK_LE;
  if (strcmp(op, ">=") == 0) return TK_GE;
  if (strcmp(op, "&&") == 0) return TK_ANDAND;
  if (strcmp(op, "||") == 0) return TK_OROR;
  if (strcmp(op, "##") == 0) return TK_HASHHASH;
  if (strcmp(op, "++") == 0) return TK_INC;
  if (strcmp(op, "--") == 0) return TK_DEC;
  if (strcmp(op, "<<") == 0) return TK_SHL;
  if (strcmp(op, ">>") == 0) return TK_SHR;
  if (strcmp(op, "->") == 0) return TK_ARROW;
  if (strcmp(op, "+=") == 0) return TK_PLUSEQ;
  if (strcmp(op, "-=") == 0) return TK_MINUSEQ;
  if (strcmp(op, "*=") == 0) return TK_MULEQ;
  if (strcmp(op, "/=") == 0) return TK_DIVEQ;
  if (strcmp(op, "%=") == 0) return TK_MODEQ;
  if (strcmp(op, "<<=") == 0) return TK_SHLEQ;
  if (strcmp(op, ">>=") == 0) return TK_SHREQ;
  if (strcmp(op, "&=") == 0) return TK_ANDEQ;
  if (strcmp(op, "^=") == 0) return TK_XOREQ;
  if (strcmp(op, "|=") == 0) return TK_OREQ;
  if (strcmp(op, "...") == 0) return TK_ELLIPSIS;
  if (strcmp(op, "%:%:") == 0) return TK_HASHHASH;
  if (strcmp(op, "<") == 0) return TK_LT;
  if (strcmp(op, ">") == 0) return TK_GT;
  if (strcmp(op, "+") == 0) return TK_PLUS;
  if (strcmp(op, "-") == 0) return TK_MINUS;
  if (strcmp(op, "*") == 0) return TK_MUL;
  if (strcmp(op, "/") == 0) return TK_DIV;
  if (strcmp(op, "%") == 0) return TK_MOD;
  if (strcmp(op, "!") == 0) return TK_BANG;
  if (strcmp(op, "~") == 0) return TK_TILDE;
  if (strcmp(op, "=") == 0) return TK_ASSIGN;
  if (strcmp(op, "(") == 0) return TK_LPAREN;
  if (strcmp(op, ")") == 0) return TK_RPAREN;
  if (strcmp(op, "{") == 0) return TK_LBRACE;
  if (strcmp(op, "}") == 0) return TK_RBRACE;
  if (strcmp(op, "[") == 0) return TK_LBRACKET;
  if (strcmp(op, "]") == 0) return TK_RBRACKET;
  if (strcmp(op, ",") == 0) return TK_COMMA;
  if (strcmp(op, ";") == 0) return TK_SEMI;
  if (strcmp(op, "&") == 0) return TK_AMP;
  if (strcmp(op, "|") == 0) return TK_PIPE;
  if (strcmp(op, "^") == 0) return TK_CARET;
  if (strcmp(op, "?") == 0) return TK_QUESTION;
  if (strcmp(op, ":") == 0) return TK_COLON;
  if (strcmp(op, "#") == 0) return TK_HASH;
  if (strcmp(op, "<:") == 0) return TK_LBRACKET;
  if (strcmp(op, ":>") == 0) return TK_RBRACKET;
  if (strcmp(op, "<%") == 0) return TK_LBRACE;
  if (strcmp(op, "%>") == 0) return TK_RBRACE;
  if (strcmp(op, "%:") == 0) return TK_HASH;
  if (strcmp(op, ".") == 0) return TK_DOT;
  return TK_EOF;
}

bool consume(char op) {
  token_kind_t kind = kind_for_char(op);
  if (kind == TK_EOF || token->kind != kind)
    return false;
  token = token->next;
  return true;
}

bool consume_str(char *op) {
  token_kind_t kind = kind_for_op(op);
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

static token_t *new_token_simple(token_kind_t kind, token_t *cur, int line_no) {
  token_pp_t *tok = calloc(1, sizeof(token_pp_t));
  init_token_base(&tok->base, kind, line_no);
  cur->next = (token_t *)tok;
  return (token_t *)tok;
}

static token_ident_t *new_token_ident(token_t *cur, char *str, int len, int line_no) {
  token_ident_t *tok = calloc(1, sizeof(token_ident_t));
  init_token_base(&tok->pp.base, TK_IDENT, line_no);
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_string_t *new_token_string(token_t *cur, char *str, int len, int line_no) {
  token_string_t *tok = calloc(1, sizeof(token_string_t));
  init_token_base(&tok->pp.base, TK_STRING, line_no);
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static token_num_t *new_token_num(token_t *cur, char *str, int len, int line_no) {
  token_num_t *tok = calloc(1, sizeof(token_num_t));
  init_token_base(&tok->pp.base, TK_NUM, line_no);
  tok->str = str;
  tok->len = len;
  cur->next = (token_t *)tok;
  return tok;
}

static int read_escape_char(char **pp) {
  char *p = *pp;
  if (*p == 'a') { *pp = p + 1; return '\a'; }
  if (*p == 'b') { *pp = p + 1; return '\b'; }
  if (*p == 'f') { *pp = p + 1; return '\f'; }
  if (*p == 'n') { *pp = p + 1; return '\n'; }
  if (*p == 'r') { *pp = p + 1; return '\r'; }
  if (*p == 't') { *pp = p + 1; return '\t'; }
  if (*p == 'v') { *pp = p + 1; return '\v'; }
  if (*p == '\\') { *pp = p + 1; return '\\'; }
  if (*p == '\'') { *pp = p + 1; return '\''; }
  if (*p == '"') { *pp = p + 1; return '"'; }
  if (*p == '?') { *pp = p + 1; return '?'; }
  if (*p == 'x') {
    p++;
    if (!isxdigit(*p)) error_at(p, "16進エスケープが不正です");
    unsigned valx = 0;
    while (isxdigit(*p)) {
      int digit;
      if ('0' <= *p && *p <= '9') digit = *p - '0';
      else if ('a' <= *p && *p <= 'f') digit = *p - 'a' + 10;
      else digit = *p - 'A' + 10;
      valx = valx * 16 + digit;
      p++;
    }
    if (valx > 255) error_at(p, "エスケープが大きすぎます");
    *pp = p;
    return (int)valx;
  }
  if (*p == 'u' || *p == 'U') {
    int digits = (*p == 'u') ? 4 : 8;
    p++;
    unsigned long val = 0;
    for (int i = 0; i < digits; i++) {
      if (!isxdigit((unsigned char)p[i])) {
        error_at(p + i, "UCNエスケープが不正です");
      }
      int digit;
      if ('0' <= p[i] && p[i] <= '9') digit = p[i] - '0';
      else if ('a' <= p[i] && p[i] <= 'f') digit = p[i] - 'a' + 10;
      else digit = p[i] - 'A' + 10;
      val = val * 16 + (unsigned)digit;
    }
    if (val > 0x10FFFF) error_at(p, "UCNエスケープが不正です");
    p += digits;
    *pp = p;
    return (int)(val & 0xFF);
  }
  if (*p >= '0' && *p <= '7') {
    int cnt = 0;
    unsigned valo = 0;
    while (cnt < 3 && *p >= '0' && *p <= '7') {
      valo = valo * 8 + (*p - '0');
      p++;
      cnt++;
    }
    if (valo > 255) error_at(p, "エスケープが大きすぎます");
    *pp = p;
    return (int)valo;
  }
  error_at(p, "不正なエスケープです");
  *pp = p + 1;
  return (unsigned char)*p;
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

  if (isalpha(*p) || *p == '_') error_at(p, "整数サフィックスが不正です");

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

static int string_prefix_len(const char *p) {
  if (p[0] == 'u' && p[1] == '8' && p[2] == '"') return 2;
  if ((p[0] == 'L' || p[0] == 'u' || p[0] == 'U') && p[1] == '"') return 1;
  return 0;
}

static int char_prefix_len(const char *p) {
  if ((p[0] == 'L' || p[0] == 'u' || p[0] == 'U') && p[1] == '\'') return 1;
  return 0;
}

static bool is_ident_start(const char *p, int *adv) {
  int ucn_len = 0;
  if (isalpha((unsigned char)*p) || *p == '_') {
    *adv = 1;
    return true;
  }
  if (starts_with_ucn(p, &ucn_len)) {
    *adv = ucn_len;
    return true;
  }
  return false;
}

static bool is_ident_continue(const char *p, int *adv) {
  int ucn_len = 0;
  if (isalnum((unsigned char)*p) || *p == '_') {
    *adv = 1;
    return true;
  }
  if (starts_with_ucn(p, &ucn_len)) {
    *adv = ucn_len;
    return true;
  }
  return false;
}

// 文字列 p をトークナイズしてその結果へのポインタを返す
token_t *tokenize(char *p) {
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
    // 行継続（バックスラッシュ + 改行）を除去
    if (*p == '\\' && p[1] == '\n') {
      p += 2;
      line_no++;
      continue;
    }

    // 空白文字をスキップ
    if (isspace(*p)) {
      has_space = true;
      if (*p == '\n') {
        at_bol = true;
        line_no++;
      }
      p++;
      continue;
    }

    // 行コメント // ... \n
    if (*p == '/' && p[1] == '/') {
      has_space = true;
      p += 2;
      while (*p && *p != '\n')
        p++;
      if (*p == '\n') {
        at_bol = true;
        line_no++;
        p++;
      }
      continue;
    }

    // ブロックコメント /* ... */
    if (*p == '/' && p[1] == '*') {
      has_space = true;
      p += 2;
      bool closed = false;
      while (*p) {
        if (*p == '\n') {
          at_bol = true;
          line_no++;
        }
        if (*p == '*' && p[1] == '/') {
          p += 2;
          closed = true;
          break;
        }
        p++;
      }
      if (!closed) {
        error_at(p, "コメントが閉じられていません");
      }
      continue;
    }

    // 新しいトークンの処理前にフラグを覚えておく
    bool _at_bol = at_bol;
    bool _has_space = has_space;
    at_bol = false;
    has_space = false;

    // 3文字の演算子 (..., <<=, >>=, %:%:)
    if (strncmp(p, "...", 3) == 0 || strncmp(p, "<<=", 3) == 0 ||
        strncmp(p, ">>=", 3) == 0 || strncmp(p, "%:%:", 4) == 0) {
      token_kind_t kind = TK_EOF;
      if (strncmp(p, "...", 3) == 0) kind = TK_ELLIPSIS;
      else if (strncmp(p, "<<=", 3) == 0) kind = TK_SHLEQ;
      else if (strncmp(p, ">>=", 3) == 0) kind = TK_SHREQ;
      else if (strncmp(p, "%:%:", 4) == 0) kind = TK_HASHHASH;
      cur = new_token_simple(kind, cur, line_no);
      cur->at_bol = _at_bol;
      cur->has_space = _has_space;
      p += (kind == TK_HASHHASH) ? 4 : 3;
      continue;
    }

    // 2文字の演算子
    if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0 ||
        strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0 ||
        strncmp(p, "&&", 2) == 0 || strncmp(p, "||", 2) == 0 ||
        strncmp(p, "##", 2) == 0 || strncmp(p, "++", 2) == 0 ||
        strncmp(p, "--", 2) == 0 || strncmp(p, "<<", 2) == 0 ||
        strncmp(p, ">>", 2) == 0 || strncmp(p, "->", 2) == 0 ||
        strncmp(p, "+=", 2) == 0 || strncmp(p, "-=", 2) == 0 ||
        strncmp(p, "*=", 2) == 0 || strncmp(p, "/=", 2) == 0 ||
        strncmp(p, "%=", 2) == 0 || strncmp(p, "&=", 2) == 0 ||
        strncmp(p, "^=", 2) == 0 || strncmp(p, "|=", 2) == 0 ||
        strncmp(p, "<:", 2) == 0 || strncmp(p, ":>", 2) == 0 ||
        strncmp(p, "<%", 2) == 0 || strncmp(p, "%>", 2) == 0 ||
        strncmp(p, "%:", 2) == 0) {
      token_kind_t kind = TK_EOF;
      if (strncmp(p, "==", 2) == 0) kind = TK_EQEQ;
      else if (strncmp(p, "!=", 2) == 0) kind = TK_NEQ;
      else if (strncmp(p, "<=", 2) == 0) kind = TK_LE;
      else if (strncmp(p, ">=", 2) == 0) kind = TK_GE;
      else if (strncmp(p, "&&", 2) == 0) kind = TK_ANDAND;
      else if (strncmp(p, "||", 2) == 0) kind = TK_OROR;
      else if (strncmp(p, "##", 2) == 0) kind = TK_HASHHASH;
      else if (strncmp(p, "++", 2) == 0) kind = TK_INC;
      else if (strncmp(p, "--", 2) == 0) kind = TK_DEC;
      else if (strncmp(p, "<<", 2) == 0) kind = TK_SHL;
      else if (strncmp(p, ">>", 2) == 0) kind = TK_SHR;
      else if (strncmp(p, "->", 2) == 0) kind = TK_ARROW;
      else if (strncmp(p, "+=", 2) == 0) kind = TK_PLUSEQ;
      else if (strncmp(p, "-=", 2) == 0) kind = TK_MINUSEQ;
      else if (strncmp(p, "*=", 2) == 0) kind = TK_MULEQ;
      else if (strncmp(p, "/=", 2) == 0) kind = TK_DIVEQ;
      else if (strncmp(p, "%=", 2) == 0) kind = TK_MODEQ;
      else if (strncmp(p, "&=", 2) == 0) kind = TK_ANDEQ;
      else if (strncmp(p, "^=", 2) == 0) kind = TK_XOREQ;
      else if (strncmp(p, "|=", 2) == 0) kind = TK_OREQ;
      else if (strncmp(p, "<:", 2) == 0) kind = TK_LBRACKET;
      else if (strncmp(p, ":>", 2) == 0) kind = TK_RBRACKET;
      else if (strncmp(p, "<%", 2) == 0) kind = TK_LBRACE;
      else if (strncmp(p, "%>", 2) == 0) kind = TK_RBRACE;
      else if (strncmp(p, "%:", 2) == 0) kind = TK_HASH;
      cur = new_token_simple(kind, cur, line_no);
      cur->at_bol = _at_bol;
      cur->has_space = _has_space;
      p += 2;
      continue;
    }
    // 文字列リテラル（接頭辞 L/u/U/u8 を含む）
    int str_prefix = string_prefix_len(p);
    if (*p == '"' || str_prefix > 0) {
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
          read_escape_char(&p);
          continue;
        }
        p++;
      }
      int len = p - start;
      p++; // 閉じ引用符をスキップ
      token_string_t *st = new_token_string(cur, start, len, line_no);
      st->pp.base.at_bol = _at_bol;
      st->pp.base.has_space = _has_space;
      cur = (token_t *)st;
      continue;
    }

    // 文字リテラル（接頭辞 L/u/U を含む、マルチ文字定数対応）
    int chr_prefix = char_prefix_len(p);
    if (*p == '\'' || chr_prefix > 0) {
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
      while (*p && *p != '\'') {
        int one = 0;
        if (*p == '\\') {
          p++;
          one = read_escape_char(&p);
        } else if (*p == '\n') {
          error_at(p, "文字リテラルが不正です");
        } else {
          one = (unsigned char)*p;
          p++;
        }
        ch = ((ch << 8) | (unsigned char)one) & 0xFFFFFFFFULL;
        nchar++;
      }
      if (nchar == 0 || *p != '\'') {
        error_at(p, "文字リテラルが不正です");
      }
      p++; // 閉じクォートをスキップ
      int len = p - start;
      token_num_t *num = new_token_num(cur, start, len, line_no);
      num->uval = ch;
      num->val = (long long)ch;
      num->is_float = 0;
      num->int_base = 10;
      num->is_unsigned = false;
      num->int_size = 0;
      num->pp.base.at_bol = _at_bol;
      num->pp.base.has_space = _has_space;
      cur = (token_t *)num;
      continue;
    }

    // 1文字の記号 (+, -, *, /, %, (, ), <, >, ;, =, {, }, ,, &, [, ], #, ., !, ~, |, ^, ?, :)
    if (strchr("+-*/%()<>;={},&[]#.!~|^?:", *p) || (*p == '.' && !isdigit(p[1]))) {
      token_kind_t kind = kind_for_char(*p);
      cur = new_token_simple(kind, cur, line_no);
      cur->at_bol = _at_bol;
      cur->has_space = _has_space;
      p++;
      continue;
    }

    // 識別子またはキーワード (a〜z で始まる連続した英字)
    // 識別子・キーワード（英字または_で始まり、英数字または_が続く）
    int adv = 0;
    if (is_ident_start(p, &adv)) {
      char *start = p;
      p += adv;
      while (is_ident_continue(p, &adv))
        p += adv;
      int len = p - start;

      // キーワード判定
      static const struct {
        const char *name;
        int len;
        token_kind_t kind;
      } kw[] = {
        {"if", 2, TK_IF},
        {"else", 4, TK_ELSE},
        {"while", 5, TK_WHILE},
        {"for", 3, TK_FOR},
        {"return", 6, TK_RETURN},
        {"auto", 4, TK_AUTO},
        {"break", 5, TK_BREAK},
        {"case", 4, TK_CASE},
        {"const", 5, TK_CONST},
        {"continue", 8, TK_CONTINUE},
        {"default", 7, TK_DEFAULT},
        {"do", 2, TK_DO},
        {"enum", 4, TK_ENUM},
        {"extern", 6, TK_EXTERN},
        {"goto", 4, TK_GOTO},
        {"inline", 6, TK_INLINE},
        {"int", 3, TK_INT},
        {"register", 8, TK_REGISTER},
        {"restrict", 8, TK_RESTRICT},
        {"signed", 6, TK_SIGNED},
        {"sizeof", 6, TK_SIZEOF},
        {"static", 6, TK_STATIC},
        {"struct", 6, TK_STRUCT},
        {"switch", 6, TK_SWITCH},
        {"typedef", 7, TK_TYPEDEF},
        {"union", 5, TK_UNION},
        {"unsigned", 8, TK_UNSIGNED},
        {"volatile", 8, TK_VOLATILE},
        {"char", 4, TK_CHAR},
        {"void", 4, TK_VOID},
        {"short", 5, TK_SHORT},
        {"long", 4, TK_LONG},
        {"float", 5, TK_FLOAT},
        {"double", 6, TK_DOUBLE},
        {"_Alignas", 8, TK_ALIGNAS},
        {"_Alignof", 8, TK_ALIGNOF},
        {"_Atomic", 7, TK_ATOMIC},
        {"_Bool", 5, TK_BOOL},
        {"_Complex", 8, TK_COMPLEX},
        {"_Generic", 8, TK_GENERIC},
        {"_Imaginary", 10, TK_IMAGINARY},
        {"_Noreturn", 9, TK_NORETURN},
        {"_Static_assert", 14, TK_STATIC_ASSERT},
        {"_Thread_local", 13, TK_THREAD_LOCAL},
      };

      bool is_kw = false;
      for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        if (len == kw[i].len && strncmp(start, kw[i].name, len) == 0) {
          cur = new_token_simple(kw[i].kind, cur, line_no);
          cur->at_bol = _at_bol;
          cur->has_space = _has_space;
          is_kw = true;
          break;
        }
      }

      if (!is_kw) {
        token_ident_t *id = new_token_ident(cur, start, len, line_no);
        id->pp.base.at_bol = _at_bol;
        id->pp.base.has_space = _has_space;
        cur = (token_t *)id;
      }
      continue;
    }

    // 数値リテラル (整数 または 浮動小数点数)
    if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
      char *start = p; // Keep track of the start of the number for length calculation
      token_num_t *num = new_token_num(cur, p, 0, line_no);
      num->pp.base.at_bol = _at_bol;
      num->pp.base.has_space = _has_space;

      // 16進数/2進数/8進数/10進数
      if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        // 16進数 (整数 or 浮動小数点)
        bool is_hex_float = false;
        for (char *q = p + 2; isxdigit(*q) || *q == '.' || *q == 'p' || *q == 'P'; q++) {
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
          if (isalpha(*end) || *end == '_') error_at(end, "浮動小数点サフィックスが不正です");
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
        if (strict_c11_mode) {
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
      } else if (*p == '0' && isdigit(p[1])) {
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
        char *q = p;
        // 浮動小数点数の判定 (小数点 '.' または指数 'e'/'E' が含まれるか)
        bool is_float = false;
        while (isalnum(*q) || *q == '.') {
          if (*q == '.' || *q == 'e' || *q == 'E') {
            is_float = true;
          }
          q++;
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
          if (isalpha(*end) || *end == '_') error_at(end, "浮動小数点サフィックスが不正です");
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
      if (*p == '.' || isalnum((unsigned char)*p) || *p == '_') {
        error_at(p, "数値リテラルが不正です");
      }
      num->len = p - start;
      num->str = start;
      cur = (token_t *)num;
      continue;
    }

    error_at(p, "トークナイズできません");
  }

  new_token_simple(TK_EOF, cur, line_no);
  return head.next;
}
