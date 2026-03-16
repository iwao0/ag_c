#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "token.h"
#include <stddef.h>

// 現在着目しているトークン
extern token_t *token;

// エラーを報告する関数
void tk_error_at(char *loc, char *fmt, ...);
void tk_error_tok(token_t *tok, char *fmt, ...);
const char *tk_token_kind_str(token_kind_t kind, int *len);

// 次のトークンが期待している記号のときには、トークンを1つ読み進めて真を返す。
// それ以外の場合には偽を返す。
bool tk_consume(char op);
bool tk_consume_str(char *op);
token_ident_t *tk_consume_ident(void);

// 次のトークンが期待している記号のときには、トークンを1つ読み進める。
// それ以外の場合にはエラーを報告する。
void tk_expect(char op);

// 次のトークンが数値の場合、トークンを1つ読み進めてその数値を返す。
// それ以外の場合にはエラーを報告する。
int tk_expect_number(void);

// トークンが入力の終わり(EOF)かを判定する。
bool tk_at_eof(void);

// 入力文字列 p をトークナイズしてそれを返す
token_t *tk_tokenize(char *p);

// 現在の入力文字列を取得・設定
char *tk_get_user_input(void);
void tk_set_user_input(char *p);

// 現在のファイル名を取得・設定
char *tk_get_filename(void);
void tk_set_filename(char *name);

// strict C11 モード（拡張機能の許可/禁止）
bool tk_get_strict_c11_mode(void);
void tk_set_strict_c11_mode(bool strict);
bool tk_get_enable_trigraphs(void);
void tk_set_enable_trigraphs(bool enable);
bool tk_get_enable_binary_literals(void);
void tk_set_enable_binary_literals(bool enable);
bool tk_get_enable_c11_audit_extensions(void);
void tk_set_enable_c11_audit_extensions(bool enable);

typedef struct {
  size_t alloc_count;
  size_t alloc_bytes;
  size_t peak_alloc_bytes;
} tokenizer_stats_t;

void tk_reset_tokenizer_stats(void);
tokenizer_stats_t tk_get_tokenizer_stats(void);

#endif
