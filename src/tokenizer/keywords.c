#include "internal/keywords.h"

static inline bool eq2(const char *s, const char a, const char b) {
  return s[0] == a && s[1] == b;
}

static inline bool eq3(const char *s, const char a, const char b, const char c) {
  return s[0] == a && s[1] == b && s[2] == c;
}

static inline bool eq4(const char *s, const char a, const char b, const char c, const char d) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d;
}

static inline bool eq5(const char *s, const char a, const char b, const char c, const char d, const char e) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e;
}

static inline bool eq6(const char *s, const char a, const char b, const char c, const char d, const char e, const char f) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e && s[5] == f;
}

static inline bool eq7(const char *s, const char a, const char b, const char c, const char d, const char e, const char f, const char g) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e && s[5] == f && s[6] == g;
}

static inline bool eq8(const char *s, const char a, const char b, const char c, const char d, const char e, const char f, const char g, const char h) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e && s[5] == f && s[6] == g && s[7] == h;
}

static inline bool eq9(const char *s, const char a, const char b, const char c, const char d, const char e, const char f, const char g, const char h, const char i) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e && s[5] == f && s[6] == g && s[7] == h && s[8] == i;
}

static inline bool eq10(const char *s, const char a, const char b, const char c, const char d, const char e, const char f, const char g, const char h, const char i, const char j) {
  return s[0] == a && s[1] == b && s[2] == c && s[3] == d && s[4] == e && s[5] == f && s[6] == g && s[7] == h && s[8] == i && s[9] == j;
}

static inline bool eq13(const char *s) {
  return s[0] == '_' && s[1] == 'T' && s[2] == 'h' && s[3] == 'r' && s[4] == 'e' && s[5] == 'a' && s[6] == 'd' &&
         s[7] == '_' && s[8] == 'l' && s[9] == 'o' && s[10] == 'c' && s[11] == 'a' && s[12] == 'l';
}

static inline bool eq14(const char *s) {
  return s[0] == '_' && s[1] == 'S' && s[2] == 't' && s[3] == 'a' && s[4] == 't' && s[5] == 'i' && s[6] == 'c' &&
         s[7] == '_' && s[8] == 'a' && s[9] == 's' && s[10] == 's' && s[11] == 'e' && s[12] == 'r' && s[13] == 't';
}

/** @brief 文字列がC11キーワードなら対応する token kind を返す。 */
token_kind_t lookup_keyword(const char *s, int len) {
  switch (len) {
    case 2:
      if (eq2(s, 'i', 'f')) return TK_IF;
      if (eq2(s, 'd', 'o')) return TK_DO;
      return TK_EOF;
    case 3:
      if (eq3(s, 'i', 'n', 't')) return TK_INT;
      if (eq3(s, 'f', 'o', 'r')) return TK_FOR;
      return TK_EOF;
    case 4:
      if (eq4(s, 'e', 'l', 's', 'e')) return TK_ELSE;
      if (eq4(s, 'a', 'u', 't', 'o')) return TK_AUTO;
      if (eq4(s, 'c', 'a', 's', 'e')) return TK_CASE;
      if (eq4(s, 'c', 'h', 'a', 'r')) return TK_CHAR;
      if (eq4(s, 'e', 'n', 'u', 'm')) return TK_ENUM;
      if (eq4(s, 'g', 'o', 't', 'o')) return TK_GOTO;
      if (eq4(s, 'l', 'o', 'n', 'g')) return TK_LONG;
      if (eq4(s, 'v', 'o', 'i', 'd')) return TK_VOID;
      return TK_EOF;
    case 5:
      if (eq5(s, 'w', 'h', 'i', 'l', 'e')) return TK_WHILE;
      if (eq5(s, 'b', 'r', 'e', 'a', 'k')) return TK_BREAK;
      if (eq5(s, 'c', 'o', 'n', 's', 't')) return TK_CONST;
      if (eq5(s, 'f', 'l', 'o', 'a', 't')) return TK_FLOAT;
      if (eq5(s, 's', 'h', 'o', 'r', 't')) return TK_SHORT;
      if (eq5(s, 'u', 'n', 'i', 'o', 'n')) return TK_UNION;
      if (eq5(s, '_', 'B', 'o', 'o', 'l')) return TK_BOOL;
      return TK_EOF;
    case 6:
      if (eq6(s, 'r', 'e', 't', 'u', 'r', 'n')) return TK_RETURN;
      if (eq6(s, 's', 't', 'a', 't', 'i', 'c')) return TK_STATIC;
      if (eq6(s, 's', 'i', 'z', 'e', 'o', 'f')) return TK_SIZEOF;
      if (eq6(s, 's', 'i', 'g', 'n', 'e', 'd')) return TK_SIGNED;
      if (eq6(s, 's', 'w', 'i', 't', 'c', 'h')) return TK_SWITCH;
      if (eq6(s, 's', 't', 'r', 'u', 'c', 't')) return TK_STRUCT;
      if (eq6(s, 'e', 'x', 't', 'e', 'r', 'n')) return TK_EXTERN;
      if (eq6(s, 'i', 'n', 'l', 'i', 'n', 'e')) return TK_INLINE;
      if (eq6(s, 'd', 'o', 'u', 'b', 'l', 'e')) return TK_DOUBLE;
      return TK_EOF;
    case 7:
      if (eq7(s, 'd', 'e', 'f', 'a', 'u', 'l', 't')) return TK_DEFAULT;
      if (eq7(s, 't', 'y', 'p', 'e', 'd', 'e', 'f')) return TK_TYPEDEF;
      if (eq7(s, '_', 'A', 't', 'o', 'm', 'i', 'c')) return TK_ATOMIC;
      return TK_EOF;
    case 8:
      if (eq8(s, 'c', 'o', 'n', 't', 'i', 'n', 'u', 'e')) return TK_CONTINUE;
      if (eq8(s, 'r', 'e', 'g', 'i', 's', 't', 'e', 'r')) return TK_REGISTER;
      if (eq8(s, 'r', 'e', 's', 't', 'r', 'i', 'c', 't')) return TK_RESTRICT;
      if (eq8(s, 'u', 'n', 's', 'i', 'g', 'n', 'e', 'd')) return TK_UNSIGNED;
      if (eq8(s, 'v', 'o', 'l', 'a', 't', 'i', 'l', 'e')) return TK_VOLATILE;
      if (eq8(s, '_', 'A', 'l', 'i', 'g', 'n', 'a', 's')) return TK_ALIGNAS;
      if (eq8(s, '_', 'A', 'l', 'i', 'g', 'n', 'o', 'f')) return TK_ALIGNOF;
      if (eq8(s, '_', 'C', 'o', 'm', 'p', 'l', 'e', 'x')) return TK_COMPLEX;
      if (eq8(s, '_', 'G', 'e', 'n', 'e', 'r', 'i', 'c')) return TK_GENERIC;
      return TK_EOF;
    case 9:
      if (eq9(s, '_', 'N', 'o', 'r', 'e', 't', 'u', 'r', 'n')) return TK_NORETURN;
      return TK_EOF;
    case 10:
      if (eq10(s, '_', 'I', 'm', 'a', 'g', 'i', 'n', 'a', 'r', 'y')) return TK_IMAGINARY;
      return TK_EOF;
    case 13:
      if (eq13(s)) return TK_THREAD_LOCAL;
      return TK_EOF;
    case 14:
      if (eq14(s)) return TK_STATIC_ASSERT;
      return TK_EOF;
    default:
      return TK_EOF;
  }
}
