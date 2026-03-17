#ifndef TOKEN_H
#define TOKEN_H

#include <stdbool.h>
#include <stdint.h>

// 浮動小数点種別
typedef enum {
  TK_FLOAT_KIND_NONE = 0,
  TK_FLOAT_KIND_FLOAT = 1,
  TK_FLOAT_KIND_DOUBLE = 2,
  TK_FLOAT_KIND_LONG_DOUBLE = 3,
} tk_float_kind_t;

// 浮動小数点サフィックス種別
typedef enum {
  TK_FLOAT_SUFFIX_NONE = 0,
  TK_FLOAT_SUFFIX_F = 1,
  TK_FLOAT_SUFFIX_L = 2,
} tk_float_suffix_kind_t;

typedef enum {
  TK_NUM_KIND_INT = 0,
  TK_NUM_KIND_FLOAT = 1,
} tk_num_kind_t;

// 整数サイズ種別（サフィックス由来）
typedef enum {
  TK_INT_SIZE_INT = 0,
  TK_INT_SIZE_LONG = 1,
  TK_INT_SIZE_LONG_LONG = 2,
} tk_int_size_t;

// 文字幅
typedef enum {
  TK_CHAR_WIDTH_CHAR = 1,
  TK_CHAR_WIDTH_CHAR16 = 2,
  TK_CHAR_WIDTH_CHAR32 = 4,
} tk_char_width_t;

// 文字列リテラル接頭辞
typedef enum {
  TK_STR_PREFIX_NONE = 0,
  TK_STR_PREFIX_L = 1,
  TK_STR_PREFIX_u = 2,
  TK_STR_PREFIX_U = 3,
  TK_STR_PREFIX_u8 = 4,
} tk_string_prefix_kind_t;

// 文字定数接頭辞
typedef enum {
  TK_CHAR_PREFIX_NONE = 0,
  TK_CHAR_PREFIX_L = 1,
  TK_CHAR_PREFIX_u = 2,
  TK_CHAR_PREFIX_U = 3,
} tk_char_prefix_kind_t;

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
  token_t *next;      // 次の入力トークン
  token_kind_t kind;  // トークンの型（hot）
  int line_no;        // 行番号
  bool at_bol;        // 行頭(Beginning of Line)にあるか
  bool has_space;     // 直前に空白文字があるか
  char *file_name;    // ファイル名（主にエラー表示）
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
  tk_char_width_t char_width;
  tk_string_prefix_kind_t str_prefix_kind;
};

// 数値トークン
typedef struct token_num_t token_num_t;
typedef struct token_num_int_t token_num_int_t;
typedef struct token_num_float_t token_num_float_t;
/** @brief 数値トークン共通ヘッダ。実体は `num_kind` で分岐する。 */
struct token_num_t {
  token_pp_t pp;
  char *str;               // 元の文字列
  int len;                 // 元の文字列長
  // 数値トークンの実体種別（int/float 構造体へのダウンキャストに使用）
  tk_num_kind_t num_kind;
};

/** @brief 整数数値トークン本体。 */
struct token_num_int_t {
  token_num_t base;
  long long val;                // 整数値
  unsigned long long uval;      // 整数値(符号なし)
  bool is_unsigned;             // 整数サフィックス: unsigned
  tk_int_size_t int_size;
  uint8_t int_base;             // 2, 8, 10, 16
  // 文字定数由来の場合のみ有効
  tk_char_width_t char_width;
  tk_char_prefix_kind_t char_prefix_kind;
};

/** @brief 浮動小数点数値トークン本体。 */
struct token_num_float_t {
  token_num_t base;
  double fval;                  // 浮動小数点値
  tk_float_kind_t fp_kind;      // float / double / long double（整数トークンでは未使用）
  tk_float_suffix_kind_t float_suffix_kind;
};

static inline token_num_t *tk_as_num(token_t *tok) {
  return (token_num_t *)tok;
}

/** @brief `token_t*` を整数数値トークンへキャストする。 */
static inline token_num_int_t *tk_as_num_int(token_t *tok) {
  return (token_num_int_t *)tok;
}

/** @brief `token_t*` を浮動小数点数値トークンへキャストする。 */
static inline token_num_float_t *tk_as_num_float(token_t *tok) {
  return (token_num_float_t *)tok;
}

#endif
