#ifndef TOKEN_H
#define TOKEN_H

#include <stdbool.h>

// トークンの種類
typedef enum {
  TK_EOF,      // 入力の終わりを表すトークン
  TK_IDENT,    // 識別子
  TK_NUM,      // 数値トークン
  TK_STRING,   // 文字列リテラル

  // キーワード
  TK_IF,
  TK_ELSE,
  TK_WHILE,
  TK_FOR,
  TK_RETURN,
  TK_INT,
  TK_CHAR,
  TK_VOID,
  TK_SHORT,
  TK_LONG,
  TK_FLOAT,
  TK_DOUBLE,

  // 記号・演算子
  TK_LPAREN,   // (
  TK_RPAREN,   // )
  TK_LBRACE,   // {
  TK_RBRACE,   // }
  TK_LBRACKET, // [
  TK_RBRACKET, // ]
  TK_COMMA,    // ,
  TK_SEMI,     // ;
  TK_ASSIGN,   // =
  TK_PLUS,     // +
  TK_MINUS,    // -
  TK_MUL,      // *
  TK_DIV,      // /
  TK_BANG,     // !
  TK_TILDE,    // ~
  TK_LT,       // <
  TK_LE,       // <=
  TK_GT,       // >
  TK_GE,       // >=
  TK_EQEQ,     // ==
  TK_NEQ,      // !=
  TK_ANDAND,   // &&
  TK_OROR,     // ||
  TK_AMP,      // &
  TK_HASH,     // #
  TK_HASHHASH, // ##
  TK_DOT,      // .
} token_kind_t;

typedef struct hideset_t hideset_t;
struct hideset_t {
  hideset_t *next;
  char *name;
};

// 共通トークン型（最小限の共通フィールド）
typedef struct token_t token_t;
struct token_t {
  token_kind_t kind; // トークンの型
  token_t *next;     // 次の入力トークン
  bool at_bol;       // 行頭(Beginning of Line)にあるか
  bool has_space;    // 直前に空白文字があるか
  char *file_name;   // ファイル名
  int line_no;       // 行番号
};

// プリプロセッサ用の共通拡張
typedef struct token_pp_t token_pp_t;
struct token_pp_t {
  token_t base;
  hideset_t *hideset; // マクロ展開の無限ループ防止用
};

// 識別子トークン
typedef struct token_ident_t token_ident_t;
struct token_ident_t {
  token_pp_t pp;
  char *str;
  int len;
};

// 文字列リテラルトークン
typedef struct token_string_t token_string_t;
struct token_string_t {
  token_pp_t pp;
  char *str;
  int len;
};

// 数値トークン
typedef struct token_num_t token_num_t;
struct token_num_t {
  token_pp_t pp;
  int val;       // 整数値
  double fval;   // 浮動小数点値
  int is_float;  // 0=整数, 1=float, 2=double
  char *str;     // 元の文字列
  int len;       // 元の文字列長
};

#endif
