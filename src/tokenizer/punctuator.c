#include "punctuator.h"
#include <string.h>

static inline token_kind_t punctuator_kind_for_2chars(char c0, char c1);

static inline token_kind_t punctuator_kind_for_char(char c) {
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
    return punctuator_kind_for_2chars(op[0], op[1]);
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

static inline token_kind_t punctuator_kind_for_2chars(char c0, char c1) {
  static const token_kind_t table[256][256] = {
      ['!']['='] = TK_NEQ,
      ['#']['#'] = TK_HASHHASH,
      ['%']['='] = TK_MODEQ,
      ['%'][':'] = TK_HASH,
      ['%']['>'] = TK_RBRACE,
      ['&']['&'] = TK_ANDAND,
      ['&']['='] = TK_ANDEQ,
      ['+']['+'] = TK_INC,
      ['+']['='] = TK_PLUSEQ,
      ['-']['-'] = TK_DEC,
      ['-']['='] = TK_MINUSEQ,
      ['-']['>'] = TK_ARROW,
      ['*']['='] = TK_MULEQ,
      ['/']['='] = TK_DIVEQ,
      [':']['>'] = TK_RBRACKET,
      ['<']['%'] = TK_LBRACE,
      ['<'][':'] = TK_LBRACKET,
      ['<']['<'] = TK_SHL,
      ['<']['='] = TK_LE,
      ['=']['='] = TK_EQEQ,
      ['>']['='] = TK_GE,
      ['>']['>'] = TK_SHR,
      ['^']['='] = TK_XOREQ,
      ['|']['='] = TK_OREQ,
      ['|']['|'] = TK_OROR,
  };
  return table[(unsigned char)c0][(unsigned char)c1];
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
