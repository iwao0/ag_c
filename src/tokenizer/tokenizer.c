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
    error_at(token->str, "'%c'ではありません", op);
  }
  token = token->next;
}

int expect_number(void) {
  if (token->kind != TK_NUM) {
    error_at(token->str, "数ではありません");
  }
  int val = token->val;
  token = token->next;
  return val;
}

bool at_eof(void) { return token->kind == TK_EOF; }

// 新しいトークンを作成して、curに繋げる
static token_t *new_token(token_kind_t kind, token_t *cur, char *str) {
  token_t *tok = calloc(1, sizeof(token_t));
  tok->kind = kind;
  tok->str = str;
  cur->next = tok;
  return tok;
}

// 文字列 p をトークナイズしてその結果へのポインタを返す
token_t *tokenize(char *p) {
  user_input = p;
  token_t head;
  head.next = NULL;
  token_t *cur = &head;

  while (*p) {
    // 空白文字をスキップ
    if (isspace(*p)) {
      p++;
      continue;
    }

    // 2文字の演算子 (==, !=, <=, >=)
    if (strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0 ||
        strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0) {
      cur = new_token(TK_RESERVED, cur, p);
      cur->len = 2;
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
      cur = new_token(TK_STRING, cur, start);
      cur->len = len;
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
      cur = new_token(TK_NUM, cur, p);
      cur->val = ch;
      cur->len = 1;
      continue;
    }

    // 1文字の記号 (+, -, *, /, (, ), <, >, ;, =, {, }, ,, &, [, ])
    if (strchr("+-*/()<>;={},&[]", *p)) {
      cur = new_token(TK_RESERVED, cur, p++);
      cur->len = 1;
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
      if (len == 2 && strncmp(start, "if", 2) == 0) {
        cur = new_token(TK_IF, cur, start);
        cur->len = len;
      } else if (len == 4 && strncmp(start, "else", 4) == 0) {
        cur = new_token(TK_ELSE, cur, start);
        cur->len = len;
      } else if (len == 5 && strncmp(start, "while", 5) == 0) {
        cur = new_token(TK_WHILE, cur, start);
        cur->len = len;
      } else if (len == 3 && strncmp(start, "for", 3) == 0) {
        cur = new_token(TK_FOR, cur, start);
        cur->len = len;
      } else if (len == 6 && strncmp(start, "return", 6) == 0) {
        cur = new_token(TK_RETURN, cur, start);
        cur->len = len;
      } else if (len == 3 && strncmp(start, "int", 3) == 0) {
        cur = new_token(TK_INT, cur, start);
        cur->len = len;
      } else if (len == 4 && strncmp(start, "char", 4) == 0) {
        cur = new_token(TK_CHAR, cur, start);
        cur->len = len;
      } else if (len == 4 && strncmp(start, "void", 4) == 0) {
        cur = new_token(TK_VOID, cur, start);
        cur->len = len;
      } else if (len == 5 && strncmp(start, "short", 5) == 0) {
        cur = new_token(TK_SHORT, cur, start);
        cur->len = len;
      } else if (len == 4 && strncmp(start, "long", 4) == 0) {
        cur = new_token(TK_LONG, cur, start);
        cur->len = len;
      } else if (len == 5 && strncmp(start, "float", 5) == 0) {
        cur = new_token(TK_FLOAT, cur, start);
        cur->len = len;
      } else if (len == 6 && strncmp(start, "double", 6) == 0) {
        cur = new_token(TK_DOUBLE, cur, start);
        cur->len = len;
      } else {
        cur = new_token(TK_IDENT, cur, start);
        cur->len = len;
      }
      continue;
    }

    // 数値リテラル (整数 または 浮動小数点数)
    if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
      char *q = p;
      // 浮動小数点数の判定 (小数点 '.' または指数 'e'/'E' が含まれるか)
      bool is_float = false;
      while (isalnum(*q) || *q == '.') {
        if (*q == '.' || *q == 'e' || *q == 'E') {
          is_float = true;
        }
        q++;
      }
      
      cur = new_token(TK_NUM, cur, p);
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
      continue;
    }

    error_at(p, "トークナイズできません");
  }

  new_token(TK_EOF, cur, p);
  return head.next;
}
