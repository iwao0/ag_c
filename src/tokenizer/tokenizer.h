#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdbool.h>

// トークンの種類
typedef enum {
  TK_RESERVED, // 記号
  TK_IDENT,    // 識別子
  TK_NUM,      // 整数トークン
  TK_IF,       // if
  TK_ELSE,     // else
  TK_WHILE,    // while
  TK_FOR,      // for
  TK_RETURN,   // return
  TK_EOF,      // 入力の終わりを表すトークン
} token_kind_t;

// トークン型
typedef struct token_t token_t;
struct token_t {
  token_kind_t kind; // トークンの型
  token_t *next;     // 次の入力トークン
  int val;           // kindがTK_NUMの場合、その数値
  char *str;         // トークン文字列
  int len;           // トークンの長さ
};

// 現在着目しているトークン
extern token_t *token;

// エラーを報告する関数
void error_at(char *loc, char *fmt, ...);

// 次のトークンが期待している記号のときには、トークンを1つ読み進めて真を返す。
// それ以外の場合には偽を返す。
bool consume(char op);
bool consume_str(char *op);
token_t *consume_ident(void);

// 次のトークンが期待している記号のときには、トークンを1つ読み進める。
// それ以外の場合にはエラーを報告する。
void expect(char op);

// 次のトークンが数値の場合、トークンを1つ読み進めてその数値を返す。
// それ以外の場合にはエラーを報告する。
int expect_number(void);

// トークンが入力の終わり(EOF)かを判定する。
bool at_eof(void);

// 入力文字列 p をトークナイズしてそれを返す
token_t *tokenize(char *p);

#endif
