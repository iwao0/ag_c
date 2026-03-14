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
  if (tok && tok->str) {
    fprintf(stderr, " (actual: '%.*s')", tok->len, tok->str);
  }
  fprintf(stderr, "\n");
  exit(1);
}

bool consume(char op) {
  if (token->kind != TK_RESERVED || token->str[0] != op)
    return false;
  token = token->next;
  return true;
}

bool consume_str(char *op) {
  if (token->kind != TK_RESERVED || (int)strlen(op) != token->len ||
      memcmp(token->str, op, token->len))
    return false;
  token = token->next;
  return true;
}

token_t *consume_ident(void) {
  if (token->kind != TK_IDENT)
    return NULL;
  token_t *tok = token;
  token = token->next;
  return tok;
}

void expect(char op) {
  if (token->kind != TK_RESERVED || token->str[0] != op) {
    error_tok(token, "'%c'ではありません", op);
  }
  token = token->next;
}

int expect_number(void) {
  if (token->kind != TK_NUM) {
    error_tok(token, "数ではありません");
  }
  int val = token->val;
  token = token->next;
  return val;
}

bool at_eof(void) { return token->kind == TK_EOF; }

// 新しいトークンを作成して、curに繋げる
static token_t *new_token(token_kind_t kind, token_t *cur, char *str, int line_no) {
  token_t *tok = calloc(1, sizeof(token_t));
  tok->kind = kind;
  tok->str = str;
  tok->file_name = current_filename;
  tok->line_no = line_no;
  cur->next = tok;
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

    // 新しいトークンの処理前にフラグを覚えておく
    bool _at_bol = at_bol;
    bool _has_space = has_space;
    at_bol = false;
    has_space = false;

    // 2文字の演算子 (==, !=, <=, >=, &&, ||, ##)
    if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0 ||
        strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0 ||
        strncmp(p, "&&", 2) == 0 || strncmp(p, "||", 2) == 0 ||
        strncmp(p, "##", 2) == 0) {
      cur = new_token(TK_RESERVED, cur, p, line_no);
      cur->len = 2;
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
      cur = new_token(TK_STRING, cur, start, line_no);
      cur->len = len;
      cur->at_bol = _at_bol;
      cur->has_space = _has_space;
      continue;
    }

    // 文字リテラル ('c')
    if (*p == '\'') {
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
      cur = new_token(TK_NUM, cur, p, line_no);
      cur->val = ch;
      cur->len = 1;
      cur->at_bol = _at_bol;
      cur->has_space = _has_space;
      continue;
    }

    // 1文字の記号 (+, -, *, /, (, ), <, >, ;, =, {, }, ,, &, [, ], #, ., !, ~)
    if (strchr("+-*/()<>;={},&[]#.!~", *p) || (*p == '.' && !isdigit(p[1]))) {
      cur = new_token(TK_RESERVED, cur, p++, line_no);
      cur->len = 1;
      cur->at_bol = _at_bol;
      cur->has_space = _has_space;
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
          cur = new_token(kw[i].kind, cur, start, line_no);
          cur->len = len;
          cur->at_bol = _at_bol;
          cur->has_space = _has_space;
          is_kw = true;
          break;
        }
      }

      if (!is_kw) {
        cur = new_token(TK_IDENT, cur, start, line_no);
        cur->len = len;
        cur->at_bol = _at_bol;
        cur->has_space = _has_space;
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
      
      cur = new_token(TK_NUM, cur, p, line_no);
      if (is_float) {
        char *end;
        cur->fval = strtod(p, &end);
        // サフィックスの判定
        if (*end == 'f' || *end == 'F') {
          cur->is_float = 1; // float
          end++;
        } else if (*end == 'l' || *end == 'L') {
          cur->is_float = 2; // long double は未対応だが double 扱い
          end++;
        } else {
          cur->is_float = 2; // デフォルトは double
        }
        p = end;
      } else {
        cur->val = strtol(p, &p, 10);
        cur->is_float = 0;
      }
      // suffixスキップ (L, U, LL など)
      while (isalnum(*p)) p++;
      cur->len = p - start;
      continue;
    }

    error_at(p, "トークナイズできません");
  }

  new_token(TK_EOF, cur, p, line_no);
  return head.next;
}
