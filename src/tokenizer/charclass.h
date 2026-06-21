#ifndef TOKENIZER_CHARCLASS_H
#define TOKENIZER_CHARCLASS_H

#include <ctype.h>
#include <stdbool.h>

static inline bool tk_is_space(char c) {
  return isspace((unsigned char)c) != 0;
}

static inline bool tk_is_digit(char c) {
  return isdigit((unsigned char)c) != 0;
}

static inline bool tk_is_alpha(char c) {
  return isalpha((unsigned char)c) != 0;
}

static inline bool tk_is_alnum(char c) {
  return isalnum((unsigned char)c) != 0;
}

static inline bool tk_is_xdigit(char c) {
  return isxdigit((unsigned char)c) != 0;
}

static inline bool tk_is_octal_digit(char c) {
  return c >= '0' && c <= '7';
}

static inline bool tk_is_ident_start_byte(char c) {
  return tk_is_alpha(c) || c == '_';
}

static inline bool tk_is_ident_continue_byte(char c) {
  return tk_is_alnum(c) || c == '_';
}

static inline bool tk_is_punctuator1(char c) {
  switch (c) {
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case '(':
    case ')':
    case '<':
    case '>':
    case ';':
    case '=':
    case '{':
    case '}':
    case ',':
    case '&':
    case '[':
    case ']':
    case '#':
    case '.':
    case '!':
    case '~':
    case '|':
    case '^':
    case '?':
    case ':':
      return true;
    default:
      return false;
  }
}

#endif
