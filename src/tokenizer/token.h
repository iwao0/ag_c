#ifndef TOKEN_H
#define TOKEN_H

#include <stdbool.h>
#include <stdint.h>

// Floating-point kind.
typedef enum {
  TK_FLOAT_KIND_NONE = 0,
  TK_FLOAT_KIND_FLOAT = 1,
  TK_FLOAT_KIND_DOUBLE = 2,
  TK_FLOAT_KIND_LONG_DOUBLE = 3,
} tk_float_kind_t;

// Floating-point suffix kind.
typedef enum {
  TK_FLOAT_SUFFIX_NONE = 0,
  TK_FLOAT_SUFFIX_F = 1,
  TK_FLOAT_SUFFIX_L = 2,
} tk_float_suffix_kind_t;

typedef enum {
  TK_NUM_KIND_INT = 0,
  TK_NUM_KIND_FLOAT = 1,
} tk_num_kind_t;

// Integer size kind (derived from the suffix).
typedef enum {
  TK_INT_SIZE_INT = 0,
  TK_INT_SIZE_LONG = 1,
  TK_INT_SIZE_LONG_LONG = 2,
} tk_int_size_t;

// Character width.
typedef enum {
  TK_CHAR_WIDTH_CHAR = 1,
  TK_CHAR_WIDTH_CHAR16 = 2,
  TK_CHAR_WIDTH_CHAR32 = 4,
} tk_char_width_t;

// String-literal prefix.
typedef enum {
  TK_STR_PREFIX_NONE = 0,
  TK_STR_PREFIX_L = 1,
  TK_STR_PREFIX_u = 2,
  TK_STR_PREFIX_U = 3,
  TK_STR_PREFIX_u8 = 4,
} tk_string_prefix_kind_t;

// Character-constant prefix.
typedef enum {
  TK_CHAR_PREFIX_NONE = 0,
  TK_CHAR_PREFIX_L = 1,
  TK_CHAR_PREFIX_u = 2,
  TK_CHAR_PREFIX_U = 3,
} tk_char_prefix_kind_t;

// Token kinds.
typedef enum {
  TK_EOF,      // Token representing the end of input.
  TK_UNKNOWN,  // One untokenizable character (` @ $, etc.); created and discarded only in false `#if 0` branches.
               // In active code, tk_stream_next immediately emits E2028, so this never reaches the parser.
  TK_IDENT,    // Identifier.
  TK_NUM,      // Numeric token.
  TK_STRING,   // String literal.

  // Keywords.
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

  // Punctuators and operators.
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

  // Preprocessor-internal #pragma pack markers.
  TK_PRAGMA_PACK_PUSH,  // push(N) - alignment value in token_num_int_t::val.
  TK_PRAGMA_PACK_POP,   // pop
  TK_PRAGMA_PACK_SET,   // pack(N) - alignment value in token_num_int_t::val.
  TK_PRAGMA_PACK_RESET, // pack() - restore the default.
} token_kind_t;

typedef struct hideset_t hideset_t;
struct hideset_t {
  hideset_t *next;
  char *name;
};

// Common token type (minimum shared fields).
// byte_offset/byte_length describe a zero-based byte range in normalized UTF-8 input.
// token_kind_t has a small range (fewer than 256 kinds), so kind is stored as
// uint8_t.  Code reads and writes enum values directly; comparisons, switches,
// and assignments convert implicitly, and no code takes &tok->kind.
typedef struct token_t token_t;
struct token_t {
  token_t *next;             // Next input token.
  const char *source_input;  // Normalized UTF-8 source referenced by byte_offset.
  int line_no;               // Line number.
  int byte_offset;           // UTF-8 byte count from input start to token start.
  int byte_length;           // UTF-8 byte count occupied by the token (0 for EOF).
  uint16_t file_name_id;     // Index into the file-name table.
  uint8_t kind;              // Token kind (hot field storing a token_kind_t value).
  uint8_t at_bol : 1;        // Whether the token is at beginning of line.
  uint8_t has_space : 1;     // Whether whitespace immediately precedes the token.
};

// Common preprocessor extension.
typedef struct token_pp_t token_pp_t;
struct token_pp_t {
  token_t base;
  hideset_t *hideset; // Prevents infinite macro-expansion loops.
};

// Identifier token.
typedef struct token_ident_t token_ident_t;
struct token_ident_t {
  token_pp_t pp;
  char *str;
  int len;
};

// String-literal token.
// char_width and str_prefix_kind are small-range enums stored as uint8_t to
// eliminate padding (sizeof = 40B versus 48B with enum storage).  Code reads
// and writes enum values directly.
typedef struct token_string_t token_string_t;
struct token_string_t {
  token_pp_t pp;
  char *str;
  int len;
  uint8_t char_width;        // tk_char_width_t
  uint8_t str_prefix_kind;   // tk_string_prefix_kind_t
};

// Numeric token.
typedef struct token_num_t token_num_t;
typedef struct token_num_int_t token_num_int_t;
typedef struct token_num_float_t token_num_float_t;
/** @brief Common numeric-token header; `num_kind` identifies the concrete type. */
struct token_num_t {
  token_pp_t pp;
  char *str;               // Original string.
  int len;                 // Original string length.
  // Concrete numeric-token kind, used to downcast to the integer/float structure.
  tk_num_kind_t num_kind;
};

/** @brief Concrete integer numeric token.
 * base remains first because downcasts from token_t* depend on offset zero.
 * Fields after base are ordered by decreasing alignment (8, 4, 1) to remove
 * padding (sizeof = 64B versus 72B before reordering). */
struct token_num_int_t {
  token_num_t base;
  /* Anonymous union (C11 6.7.2.1) sharing one 64-bit value through signed and
   * unsigned views.  val and uval always contain the same bits; callers select
   * a view by context (the actual type is indicated by is_unsigned).  Member
   * names are promoted, so existing tok->val / tok->uval references need no change. */
  union {
    long long val;              // Integer value (signed view).
    unsigned long long uval;    // Integer value (unsigned view).
  };
  // Store small-range enums in uint8_t to avoid padding (5B total versus about 20B as enums).
  uint8_t int_size;             // tk_int_size_t
  // Valid only when derived from a character constant.
  uint8_t char_width;           // tk_char_width_t
  uint8_t char_prefix_kind;     // tk_char_prefix_kind_t
  bool is_unsigned;             // Integer suffix: unsigned.
  uint8_t int_base;             // 2, 8, 10, 16
};

/** @brief Concrete floating-point numeric token. */
struct token_num_float_t {
  token_num_t base;
  double fval;                  // Floating-point value.
  tk_float_kind_t fp_kind;      // float / double / long double (unused for integer tokens).
  tk_float_suffix_kind_t float_suffix_kind;
};

static inline token_num_t *tk_as_num(token_t *tok) {
  return (token_num_t *)tok;
}

/** @brief Cast `token_t*` to an integer numeric token. */
static inline token_num_int_t *tk_as_num_int(token_t *tok) {
  return (token_num_int_t *)tok;
}

/** @brief Cast `token_t*` to a floating-point numeric token. */
static inline token_num_float_t *tk_as_num_float(token_t *tok) {
  return (token_num_float_t *)tok;
}

#endif
