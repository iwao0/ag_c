#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "token.h"

// 現在着目しているトークン
extern token_t *token;

// エラーを報告する関数
void error_at(char *loc, char *fmt, ...);
void error_tok(token_t *tok, char *fmt, ...);
const char *token_kind_str(token_kind_t kind, int *len);

// 次のトークンが期待している記号のときには、トークンを1つ読み進めて真を返す。
// それ以外の場合には偽を返す。
bool consume(char op);
bool consume_str(char *op);
token_ident_t *consume_ident(void);

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

// 現在の入力文字列を取得・設定
char *get_user_input(void);
void set_user_input(char *p);

// 現在のファイル名を取得・設定
char *get_filename(void);
void set_filename(char *name);

// strict C11 モード（拡張機能の許可/禁止）
bool get_strict_c11_mode(void);
void set_strict_c11_mode(bool strict);
bool get_enable_trigraphs(void);
void set_enable_trigraphs(bool enable);
bool get_enable_binary_literals(void);
void set_enable_binary_literals(bool enable);

#endif
