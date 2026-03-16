#include "punctuator.h"
#include <string.h>

typedef struct {
  const char *op;
  token_kind_t kind;
  int len;
} punct_entry_t;

static const punct_entry_t puncts[] = {
  {"%:%:", TK_HASHHASH, 4},
  {"...", TK_ELLIPSIS, 3},
  {"<<=", TK_SHLEQ, 3},
  {">>=", TK_SHREQ, 3},
  {"==", TK_EQEQ, 2},
  {"!=", TK_NEQ, 2},
  {"<=", TK_LE, 2},
  {">=", TK_GE, 2},
  {"&&", TK_ANDAND, 2},
  {"||", TK_OROR, 2},
  {"##", TK_HASHHASH, 2},
  {"++", TK_INC, 2},
  {"--", TK_DEC, 2},
  {"<<", TK_SHL, 2},
  {">>", TK_SHR, 2},
  {"->", TK_ARROW, 2},
  {"+=", TK_PLUSEQ, 2},
  {"-=", TK_MINUSEQ, 2},
  {"*=", TK_MULEQ, 2},
  {"/=", TK_DIVEQ, 2},
  {"%=", TK_MODEQ, 2},
  {"&=", TK_ANDEQ, 2},
  {"^=", TK_XOREQ, 2},
  {"|=", TK_OREQ, 2},
  {"<:", TK_LBRACKET, 2},
  {":>", TK_RBRACKET, 2},
  {"<%", TK_LBRACE, 2},
  {"%>", TK_RBRACE, 2},
  {"%:", TK_HASH, 2},
  {"<", TK_LT, 1},
  {">", TK_GT, 1},
  {"+", TK_PLUS, 1},
  {"-", TK_MINUS, 1},
  {"*", TK_MUL, 1},
  {"/", TK_DIV, 1},
  {"%", TK_MOD, 1},
  {"!", TK_BANG, 1},
  {"~", TK_TILDE, 1},
  {"=", TK_ASSIGN, 1},
  {"(", TK_LPAREN, 1},
  {")", TK_RPAREN, 1},
  {"{", TK_LBRACE, 1},
  {"}", TK_RBRACE, 1},
  {"[", TK_LBRACKET, 1},
  {"]", TK_RBRACKET, 1},
  {",", TK_COMMA, 1},
  {";", TK_SEMI, 1},
  {"&", TK_AMP, 1},
  {"|", TK_PIPE, 1},
  {"^", TK_CARET, 1},
  {"?", TK_QUESTION, 1},
  {":", TK_COLON, 1},
  {"#", TK_HASH, 1},
  {".", TK_DOT, 1},
};

token_kind_t punctuator_kind_for_str(const char *op) {
  for (size_t i = 0; i < sizeof(puncts) / sizeof(puncts[0]); i++) {
    if (strcmp(op, puncts[i].op) == 0) return puncts[i].kind;
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
