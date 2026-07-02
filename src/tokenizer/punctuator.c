#include "punctuator.h"
#include <string.h>

static inline token_kind_t punctuator_kind_for_2chars(char c0, char c1);

/** @brief 1文字記号の token kind を返す (非該当は TK_EOF)。 */
token_kind_t punctuator_kind_for_char(char c) {
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

/** @brief 記号文字列の完全一致 token kind を返す。 */
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
  switch (c0) {
    case '!': return c1 == '=' ? TK_NEQ : TK_EOF;
    case '#': return c1 == '#' ? TK_HASHHASH : TK_EOF;
    case '%':
      if (c1 == '=') return TK_MODEQ;
      if (c1 == ':') return TK_HASH;
      if (c1 == '>') return TK_RBRACE;
      return TK_EOF;
    case '&':
      if (c1 == '&') return TK_ANDAND;
      if (c1 == '=') return TK_ANDEQ;
      return TK_EOF;
    case '+':
      if (c1 == '+') return TK_INC;
      if (c1 == '=') return TK_PLUSEQ;
      return TK_EOF;
    case '-':
      if (c1 == '-') return TK_DEC;
      if (c1 == '=') return TK_MINUSEQ;
      if (c1 == '>') return TK_ARROW;
      return TK_EOF;
    case '*': return c1 == '=' ? TK_MULEQ : TK_EOF;
    case '/': return c1 == '=' ? TK_DIVEQ : TK_EOF;
    case ':': return c1 == '>' ? TK_RBRACKET : TK_EOF;
    case '<':
      if (c1 == '%') return TK_LBRACE;
      if (c1 == ':') return TK_LBRACKET;
      if (c1 == '<') return TK_SHL;
      if (c1 == '=') return TK_LE;
      return TK_EOF;
    case '=': return c1 == '=' ? TK_EQEQ : TK_EOF;
    case '>':
      if (c1 == '=') return TK_GE;
      if (c1 == '>') return TK_SHR;
      return TK_EOF;
    case '^': return c1 == '=' ? TK_XOREQ : TK_EOF;
    case '|':
      if (c1 == '=') return TK_OREQ;
      if (c1 == '|') return TK_OROR;
      return TK_EOF;
    default:
      return TK_EOF;
  }
}

/** @brief `p` 位置で最長一致する記号（2〜4文字）を判定する。 */
bool match_punctuator(const char *p, token_kind_t *out_kind, int *out_len) {
  if (p[0] == '%' && p[1] == ':' && p[2] == '%' && p[3] == ':') {
    *out_kind = TK_HASHHASH;
    *out_len = 4;
    return true;
  }

  switch (p[0]) {
    case '.':
      if (p[1] == '.' && p[2] == '.') {
        *out_kind = TK_ELLIPSIS;
        *out_len = 3;
        return true;
      }
      break;
    case '<':
      if (p[1] == '<' && p[2] == '=') {
        *out_kind = TK_SHLEQ;
        *out_len = 3;
        return true;
      }
      break;
    case '>':
      if (p[1] == '>' && p[2] == '=') {
        *out_kind = TK_SHREQ;
        *out_len = 3;
        return true;
      }
      break;
  }

  token_kind_t two = punctuator_kind_for_2chars(p[0], p[1]);
  if (two != TK_EOF) {
    *out_kind = two;
    *out_len = 2;
    return true;
  }

  return false;
}
