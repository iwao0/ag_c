#include "tokenizer.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

char *get_filename(void) {
  return current_filename;
}

void set_filename(char *name) {
  current_filename = name;
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
    case TK_INT: s = "int"; break;
    case TK_CHAR: s = "char"; break;
    case TK_VOID: s = "void"; break;
    case TK_SHORT: s = "short"; break;
    case TK_LONG: s = "long"; break;
    case TK_FLOAT: s = "float"; break;
    case TK_DOUBLE: s = "double"; break;
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
  int val = ((token_num_t *)token)->val;
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

// 文字列 p をトークナイズしてその結果へのポインタを返す
token_t *tokenize(char *p) {
  user_input = p;
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
    // 文字列リテラル
    if (*p == '"') {
      p++; // 開き引用符をスキップ
      char *start = p;
      while (*p && *p != '"')
        p++;
      int len = p - start;
      if (*p == '"')
        p++; // 閉じ引用符をスキップ
      token_string_t *st = new_token_string(cur, start, len, line_no);
      st->pp.base.at_bol = _at_bol;
      st->pp.base.has_space = _has_space;
      cur = (token_t *)st;
      continue;
    }

    // 文字リテラル ('c')
    if (*p == '\'') {
      char *start = p;
      p++; // 開きクォートをスキップ
      int ch;
      if (*p == '\\') {
        p++;
        switch (*p) {
          case 'n': ch = '\n'; break;
          case 't': ch = '\t'; break;
          case '\\': ch = '\\'; break;
          case '\'': ch = '\''; break;
          case '0': ch = '\0'; break;
          default: ch = *p; break;
        }
      } else {
        ch = *p;
      }
      p++; // 文字本体をスキップ
      if (*p == '\'')
        p++; // 閉じクォートをスキップ
      int len = p - start;
      token_num_t *num = new_token_num(cur, start, len, line_no);
      num->val = ch;
      num->is_float = 0;
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
    if (isalpha(*p) || *p == '_') {
      char *start = p;
      while (isalnum(*p) || *p == '_')
        p++;
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
        {"int", 3, TK_INT},
        {"char", 4, TK_CHAR},
        {"void", 4, TK_VOID},
        {"short", 5, TK_SHORT},
        {"long", 4, TK_LONG},
        {"float", 5, TK_FLOAT},
        {"double", 6, TK_DOUBLE},
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
      char *q = p;
      // 浮動小数点数の判定 (小数点 '.' または指数 'e'/'E' が含まれるか)
      bool is_float = false;
      while (isalnum(*q) || *q == '.') {
        if (*q == '.' || *q == 'e' || *q == 'E') {
          is_float = true;
        }
        q++;
      }
      
      token_num_t *num = new_token_num(cur, p, 0, line_no);
      num->pp.base.at_bol = _at_bol;
      num->pp.base.has_space = _has_space;
      if (is_float) {
        char *end;
        num->fval = strtod(p, &end);
        // サフィックスの判定
        if (*end == 'f' || *end == 'F') {
          num->is_float = 1; // float
          end++;
        } else if (*end == 'l' || *end == 'L') {
          num->is_float = 2; // long double は未対応だが double 扱い
          end++;
        } else {
          num->is_float = 2; // デフォルトは double
        }
        p = end;
      } else {
        num->val = strtol(p, &p, 10);
        num->is_float = 0;
      }
      // suffixスキップ (L, U, LL など)
      while (isalnum(*p)) p++;
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
