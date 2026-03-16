#include "punctuator.h"
#include <stdint.h>
#include <string.h>

static token_kind_t punctuator_kind_for_char(char c) {
  switch (c) {
    case '<': return TK_LT;
    case '>': return TK_GT;
    case '+': return TK_PLUS;
    case '-': return TK_MINUS;
    case '*': return TK_MUL;
    case '/': return TK_DIV;
    case '%': return TK_MOD;
    case '!': return TK_BANG;
    case '~': return TK_TILDE;
    case '=': return TK_ASSIGN;
    case '(': return TK_LPAREN;
    case ')': return TK_RPAREN;
    case '{': return TK_LBRACE;
    case '}': return TK_RBRACE;
    case '[': return TK_LBRACKET;
    case ']': return TK_RBRACKET;
    case ',': return TK_COMMA;
    case ';': return TK_SEMI;
    case '&': return TK_AMP;
    case '|': return TK_PIPE;
    case '^': return TK_CARET;
    case '?': return TK_QUESTION;
    case ':': return TK_COLON;
    case '#': return TK_HASH;
    case '.': return TK_DOT;
    default: return TK_EOF;
  }
}

token_kind_t punctuator_kind_for_str(const char *op) {
  size_t len = strlen(op);
  if (len == 1) return punctuator_kind_for_char(op[0]);
  if (len == 2) {
    switch (op[0]) {
      case '=': if (op[1] == '=') return TK_EQEQ; break;
      case '!': if (op[1] == '=') return TK_NEQ; break;
      case '<': if (op[1] == '=') return TK_LE;
                if (op[1] == '<') return TK_SHL;
                if (op[1] == ':') return TK_LBRACKET;
                if (op[1] == '%') return TK_LBRACE;
                break;
      case '>': if (op[1] == '=') return TK_GE;
                if (op[1] == '>') return TK_SHR;
                break;
      case '&': if (op[1] == '&') return TK_ANDAND;
                if (op[1] == '=') return TK_ANDEQ;
                break;
      case '|': if (op[1] == '|') return TK_OROR;
                if (op[1] == '=') return TK_OREQ;
                break;
      case '#': if (op[1] == '#') return TK_HASHHASH; break;
      case '+': if (op[1] == '+') return TK_INC;
                if (op[1] == '=') return TK_PLUSEQ;
                break;
      case '-': if (op[1] == '-') return TK_DEC;
                if (op[1] == '>') return TK_ARROW;
                if (op[1] == '=') return TK_MINUSEQ;
                break;
      case '*': if (op[1] == '=') return TK_MULEQ; break;
      case '/': if (op[1] == '=') return TK_DIVEQ; break;
      case '%': if (op[1] == '=') return TK_MODEQ;
                if (op[1] == '>') return TK_RBRACE;
                if (op[1] == ':') return TK_HASH;
                break;
      case '^': if (op[1] == '=') return TK_XOREQ; break;
      case ':': if (op[1] == '>') return TK_RBRACKET; break;
    }
    return TK_EOF;
  }
  if (len == 3) {
    if (op[0] == '<' && op[1] == '<' && op[2] == '=') return TK_SHLEQ;
    if (op[0] == '>' && op[1] == '>' && op[2] == '=') return TK_SHREQ;
    if (op[0] == '.' && op[1] == '.' && op[2] == '.') return TK_ELLIPSIS;
    return TK_EOF;
  }
  if (len == 4) {
    if (op[0] == '%' && op[1] == ':' && op[2] == '%' && op[3] == ':') return TK_HASHHASH;
  }
  return TK_EOF;
}

typedef struct {
  uint16_t key2;
  token_kind_t kind;
} punct2_t;

static inline uint16_t punct_key2(char c0, char c1) {
  return (uint16_t)(((uint16_t)(unsigned char)c0 << 8) | (uint16_t)(unsigned char)c1);
}

static token_kind_t punctuator_kind_for_2chars(char c0, char c1) {
  static const punct2_t table[] = {
      {0x213d, TK_NEQ},      // !=
      {0x2323, TK_HASHHASH}, // ##
      {0x253d, TK_MODEQ},    // %=
      {0x253a, TK_HASH},     // %:
      {0x253e, TK_RBRACE},   // %>
      {0x2626, TK_ANDAND},   // &&
      {0x263d, TK_ANDEQ},    // &=
      {0x2b2b, TK_INC},      // ++
      {0x2b3d, TK_PLUSEQ},   // +=
      {0x2d2d, TK_DEC},      // --
      {0x2d3d, TK_MINUSEQ},  // -=
      {0x2d3e, TK_ARROW},    // ->
      {0x2a3d, TK_MULEQ},    // *=
      {0x2f3d, TK_DIVEQ},    // /=
      {0x3a3e, TK_RBRACKET}, // :>
      {0x3c25, TK_LBRACE},   // <%
      {0x3c3a, TK_LBRACKET}, // <:
      {0x3c3c, TK_SHL},      // <<
      {0x3c3d, TK_LE},       // <=
      {0x3d3d, TK_EQEQ},     // ==
      {0x3e3d, TK_GE},       // >=
      {0x3e3e, TK_SHR},      // >>
      {0x5e3d, TK_XOREQ},    // ^=
      {0x7c3d, TK_OREQ},     // |=
      {0x7c7c, TK_OROR},     // ||
  };

  uint16_t key = punct_key2(c0, c1);
  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
    if (table[i].key2 == key) return table[i].kind;
  }
  return TK_EOF;
}

bool match_punctuator(const char *p, token_kind_t *out_kind, int *out_len) {
  if (p[0] == '%' && p[1] == ':' && p[2] == '%' && p[3] == ':') {
    *out_kind = TK_HASHHASH;
    *out_len = 4;
    return true;
  }

  if (p[0] == '<' && p[1] == '<' && p[2] == '=') {
    *out_kind = TK_SHLEQ;
    *out_len = 3;
    return true;
  }
  if (p[0] == '>' && p[1] == '>' && p[2] == '=') {
    *out_kind = TK_SHREQ;
    *out_len = 3;
    return true;
  }
  if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
    *out_kind = TK_ELLIPSIS;
    *out_len = 3;
    return true;
  }

  token_kind_t two = punctuator_kind_for_2chars(p[0], p[1]);
  if (two != TK_EOF) {
    *out_kind = two;
    *out_len = 2;
    return true;
  }

  return false;
}
