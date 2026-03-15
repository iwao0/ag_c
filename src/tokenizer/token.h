#ifndef TOKEN_H
#define TOKEN_H

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

#endif
