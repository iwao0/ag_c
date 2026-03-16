#include "punctuator.h"
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

bool match_punctuator(const char *p, token_kind_t *out_kind, int *out_len) {
  switch (p[0]) {
    case '%':
      if (p[1] == ':' && p[2] == '%' && p[3] == ':') { *out_kind = TK_HASHHASH; *out_len = 4; return true; }
      if (p[1] == '=') { *out_kind = TK_MODEQ; *out_len = 2; return true; }
      if (p[1] == '>') { *out_kind = TK_RBRACE; *out_len = 2; return true; }
      if (p[1] == ':') { *out_kind = TK_HASH; *out_len = 2; return true; }
      break;
    case '.':
      if (p[1] == '.' && p[2] == '.') { *out_kind = TK_ELLIPSIS; *out_len = 3; return true; }
      break;
    case '<':
      if (p[1] == '<' && p[2] == '=') { *out_kind = TK_SHLEQ; *out_len = 3; return true; }
      if (p[1] == '<') { *out_kind = TK_SHL; *out_len = 2; return true; }
      if (p[1] == '=') { *out_kind = TK_LE; *out_len = 2; return true; }
      if (p[1] == ':') { *out_kind = TK_LBRACKET; *out_len = 2; return true; }
      if (p[1] == '%') { *out_kind = TK_LBRACE; *out_len = 2; return true; }
      break;
    case '>':
      if (p[1] == '>' && p[2] == '=') { *out_kind = TK_SHREQ; *out_len = 3; return true; }
      if (p[1] == '>') { *out_kind = TK_SHR; *out_len = 2; return true; }
      if (p[1] == '=') { *out_kind = TK_GE; *out_len = 2; return true; }
      break;
    case '=':
      if (p[1] == '=') { *out_kind = TK_EQEQ; *out_len = 2; return true; }
      break;
    case '!':
      if (p[1] == '=') { *out_kind = TK_NEQ; *out_len = 2; return true; }
      break;
    case '&':
      if (p[1] == '&') { *out_kind = TK_ANDAND; *out_len = 2; return true; }
      if (p[1] == '=') { *out_kind = TK_ANDEQ; *out_len = 2; return true; }
      break;
    case '|':
      if (p[1] == '|') { *out_kind = TK_OROR; *out_len = 2; return true; }
      if (p[1] == '=') { *out_kind = TK_OREQ; *out_len = 2; return true; }
      break;
    case '#':
      if (p[1] == '#') { *out_kind = TK_HASHHASH; *out_len = 2; return true; }
      break;
    case '+':
      if (p[1] == '+') { *out_kind = TK_INC; *out_len = 2; return true; }
      if (p[1] == '=') { *out_kind = TK_PLUSEQ; *out_len = 2; return true; }
      break;
    case '-':
      if (p[1] == '-') { *out_kind = TK_DEC; *out_len = 2; return true; }
      if (p[1] == '>') { *out_kind = TK_ARROW; *out_len = 2; return true; }
      if (p[1] == '=') { *out_kind = TK_MINUSEQ; *out_len = 2; return true; }
      break;
    case '*':
      if (p[1] == '=') { *out_kind = TK_MULEQ; *out_len = 2; return true; }
      break;
    case '/':
      if (p[1] == '=') { *out_kind = TK_DIVEQ; *out_len = 2; return true; }
      break;
    case '^':
      if (p[1] == '=') { *out_kind = TK_XOREQ; *out_len = 2; return true; }
      break;
    case ':':
      if (p[1] == '>') { *out_kind = TK_RBRACKET; *out_len = 2; return true; }
      break;
  }
  return false;
}
