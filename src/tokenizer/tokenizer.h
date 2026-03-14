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
  TK_INT,      // int
  TK_CHAR,     // char
  TK_VOID,     // void
  TK_SHORT,    // short
  TK_LONG,     // long
  TK_FLOAT,    // float
  TK_DOUBLE,   // double
  TK_STRING,   // 文字列リテラル
  TK_EOF,      // 入力の終わりを表すトークン
} token_kind_t;

typedef struct hideset_t hideset_t;
struct hideset_t {
  hideset_t *next;
  char *name;
};

// トークン型
typedef struct token_t token_t;
struct token_t {
  token_kind_t kind; // トークンの型
  token_t *next;     // 次の入力トークン
  int val;           // kindがTK_NUMの場合、その数値(整数の場合)
  double fval;       // kindがTK_NUMの場合、その数値(浮動小数点数の場合)
  int is_float;      // 0=整数, 1=float, 2=double
  char *str;         // トークン文字列
  int len;           // トークンの長さ
  bool at_bol;       // 行頭(Beginning of Line)にあるか
  bool has_space;    // 直前に空白文字があるか
  hideset_t *hideset; // マクロ展開の無限ループ防止用
  char *file_name;    // ファイル名
  int line_no;        // 行番号
};

// 現在着目しているトークン
extern token_t *token;

// エラーを報告する関数
void error_at(char *loc, char *fmt, ...);
void error_tok(token_t *tok, char *fmt, ...);

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

// 現在の入力文字列を取得・設定
char *get_user_input(void);
void set_user_input(char *p);

// 現在のファイル名を取得・設定
char *get_filename(void);
void set_filename(char *name);

#endif
