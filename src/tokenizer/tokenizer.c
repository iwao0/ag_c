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

    // 1文字の記号 (+, -, *, /, (, ), <, >, ;, =)
    if (strchr("+-*/()<>;=", *p)) {
      cur = new_token(TK_RESERVED, cur, p++);
      cur->len = 1;
      continue;
    }

    // 識別子またはキーワード (a〜z で始まる連続した英字)
    if ('a' <= *p && *p <= 'z') {
      char *start = p;
      while ('a' <= *p && *p <= 'z')
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
      } else {
        cur = new_token(TK_IDENT, cur, start);
        cur->len = len;
      }
      continue;
    }

    if (isdigit(*p)) {
      cur = new_token(TK_NUM, cur, p);
      cur->val = strtol(p, &p, 10);
      continue;
    }

    error_at(p, "トークナイズできません");
  }

  new_token(TK_EOF, cur, p);
  return head.next;
}
