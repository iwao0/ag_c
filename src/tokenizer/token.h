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
  TK_AUTO,
  TK_BREAK,
  TK_CASE,
  TK_CONST,
  TK_CONTINUE,
  TK_DEFAULT,
  TK_DO,
  TK_ENUM,
  TK_EXTERN,
  TK_GOTO,
  TK_INLINE,
  TK_INT,
  TK_REGISTER,
  TK_RESTRICT,
  TK_SIGNED,
  TK_SIZEOF,
  TK_STATIC,
  TK_STRUCT,
  TK_SWITCH,
  TK_TYPEDEF,
  TK_UNION,
  TK_UNSIGNED,
  TK_VOLATILE,
  TK_CHAR,
  TK_VOID,
  TK_SHORT,
  TK_LONG,
  TK_FLOAT,
  TK_DOUBLE,
  TK_ALIGNAS,       // _Alignas
  TK_ALIGNOF,       // _Alignof
  TK_ATOMIC,        // _Atomic
  TK_BOOL,          // _Bool
  TK_COMPLEX,       // _Complex
  TK_GENERIC,       // _Generic
  TK_IMAGINARY,     // _Imaginary
  TK_NORETURN,      // _Noreturn
  TK_STATIC_ASSERT, // _Static_assert
  TK_THREAD_LOCAL,  // _Thread_local

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
  TK_MOD,      // %
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
  TK_PIPE,     // |
  TK_CARET,    // ^
  TK_QUESTION, // ?
  TK_COLON,    // :
  TK_INC,      // ++
  TK_DEC,      // --
  TK_SHL,      // <<
  TK_SHR,      // >>
  TK_ARROW,    // ->
  TK_PLUSEQ,   // +=
  TK_MINUSEQ,  // -=
  TK_MULEQ,    // *=
  TK_DIVEQ,    // /=
  TK_MODEQ,    // %=
  TK_SHLEQ,    // <<=
  TK_SHREQ,    // >>=
  TK_ANDEQ,    // &=
  TK_XOREQ,    // ^=
  TK_OREQ,     // |=
  TK_ELLIPSIS, // ...
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
  token_t *next;     // 次の入力トークン
  char *file_name;   // ファイル名
  int line_no;       // 行番号
  token_kind_t kind; // トークンの型
  bool at_bol;       // 行頭(Beginning of Line)にあるか
  bool has_space;    // 直前に空白文字があるか
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
  // 1=char/u8, 2=char16_t, 4=char32_t/wchar_t(Apple)
  int char_width;
  // 0=ordinary, 1=L, 2=u, 3=U, 4=u8
  int str_prefix_kind;
};

// 数値トークン
typedef struct token_num_t token_num_t;
struct token_num_t {
  token_pp_t pp;
  long long val;   // 整数値
  unsigned long long uval; // 整数値(符号なし)
  double fval;     // 浮動小数点値
  int is_float;    // 0=整数, 1=float, 2=double
  bool is_unsigned; // 整数サフィックス: unsigned
  int int_size;    // 0=int, 1=long, 2=long long
  int int_base;    // 2, 8, 10, 16
  char *str;       // 元の文字列
  int len;         // 元の文字列長
  // 文字定数由来の場合のみ有効
  int char_width;
  // 0=none, 1=L, 2=u, 3=U
  int char_prefix_kind;
};

#endif
