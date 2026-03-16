/* C code produced by gperf version 3.0.3 */
/* Command-line: /Library/Developer/CommandLineTools/usr/bin/gperf /tmp/keywords.gperf  */
/* Computed positions: -k'1,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 12 "/tmp/keywords.gperf"

#include "token.h"
#include <string.h>

typedef struct {
  const char *name;
  token_kind_t kind;
} keyword_entry_t;
#line 21 "/tmp/keywords.gperf"
#include <string.h>

#define TOTAL_KEYWORDS 44
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 14
#define MIN_HASH_VALUE 5
#define MAX_HASH_VALUE 69
/* maximum key range = 65, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
keyword_hash (register const char *str, register unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70,  0, 70, 40, 15,  5,
      15, 20, 10, 20, 20, 40, 70, 10, 45, 25,
      10, 10, 70, 70, 15,  0,  0, 30, 20,  0,
      55, 10, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70
    };
  return len + asso_values[(unsigned char)str[len - 1]] + asso_values[(unsigned char)str[0]];
}

static const unsigned char lengthtable[] =
  {
     0,  0,  0,  0,  0,  5,  6,  0,  8,  0,  5,  6,  7,  8,
    14,  5,  6,  7,  8,  9, 10,  6,  7,  8,  4,  5,  6,  2,
     3,  4,  5,  6,  0,  8,  4,  0,  6,  0,  8,  4,  0,  6,
     0,  3,  4,  5,  0,  0,  8,  4,  5,  0,  2,  8,  4,  0,
     0,  0, 13,  0,  0,  0,  0,  8,  0,  0,  6,  0,  0,  4
  };

static const keyword_entry_t wordlist[] =
  {
    {"",0}, {"",0}, {"",0}, {"",0}, {"",0},
#line 53 "/tmp/keywords.gperf"
    {"short", TK_SHORT},
#line 45 "/tmp/keywords.gperf"
    {"struct", TK_STRUCT},
    {"",0},
#line 57 "/tmp/keywords.gperf"
    {"_Alignas", TK_ALIGNAS},
    {"",0},
#line 31 "/tmp/keywords.gperf"
    {"const", TK_CONST},
#line 44 "/tmp/keywords.gperf"
    {"static", TK_STATIC},
#line 59 "/tmp/keywords.gperf"
    {"_Atomic", TK_ATOMIC},
#line 62 "/tmp/keywords.gperf"
    {"_Generic", TK_GENERIC},
#line 65 "/tmp/keywords.gperf"
    {"_Static_assert", TK_STATIC_ASSERT},
#line 55 "/tmp/keywords.gperf"
    {"float", TK_FLOAT},
#line 43 "/tmp/keywords.gperf"
    {"sizeof", TK_SIZEOF},
#line 47 "/tmp/keywords.gperf"
    {"typedef", TK_TYPEDEF},
#line 58 "/tmp/keywords.gperf"
    {"_Alignof", TK_ALIGNOF},
#line 64 "/tmp/keywords.gperf"
    {"_Noreturn", TK_NORETURN},
#line 63 "/tmp/keywords.gperf"
    {"_Imaginary", TK_IMAGINARY},
#line 42 "/tmp/keywords.gperf"
    {"signed", TK_SIGNED},
#line 33 "/tmp/keywords.gperf"
    {"default", TK_DEFAULT},
#line 41 "/tmp/keywords.gperf"
    {"restrict", TK_RESTRICT},
#line 51 "/tmp/keywords.gperf"
    {"char", TK_CHAR},
#line 25 "/tmp/keywords.gperf"
    {"while", TK_WHILE},
#line 46 "/tmp/keywords.gperf"
    {"switch", TK_SWITCH},
#line 34 "/tmp/keywords.gperf"
    {"do", TK_DO},
#line 26 "/tmp/keywords.gperf"
    {"for", TK_FOR},
#line 30 "/tmp/keywords.gperf"
    {"case", TK_CASE},
#line 29 "/tmp/keywords.gperf"
    {"break", TK_BREAK},
#line 27 "/tmp/keywords.gperf"
    {"return", TK_RETURN},
    {"",0},
#line 32 "/tmp/keywords.gperf"
    {"continue", TK_CONTINUE},
#line 37 "/tmp/keywords.gperf"
    {"goto", TK_GOTO},
    {"",0},
#line 36 "/tmp/keywords.gperf"
    {"extern", TK_EXTERN},
    {"",0},
#line 40 "/tmp/keywords.gperf"
    {"register", TK_REGISTER},
#line 52 "/tmp/keywords.gperf"
    {"void", TK_VOID},
    {"",0},
#line 56 "/tmp/keywords.gperf"
    {"double", TK_DOUBLE},
    {"",0},
#line 39 "/tmp/keywords.gperf"
    {"int", TK_INT},
#line 24 "/tmp/keywords.gperf"
    {"else", TK_ELSE},
#line 48 "/tmp/keywords.gperf"
    {"union", TK_UNION},
    {"",0}, {"",0},
#line 50 "/tmp/keywords.gperf"
    {"volatile", TK_VOLATILE},
#line 35 "/tmp/keywords.gperf"
    {"enum", TK_ENUM},
#line 60 "/tmp/keywords.gperf"
    {"_Bool", TK_BOOL},
    {"",0},
#line 23 "/tmp/keywords.gperf"
    {"if", TK_IF},
#line 49 "/tmp/keywords.gperf"
    {"unsigned", TK_UNSIGNED},
#line 28 "/tmp/keywords.gperf"
    {"auto", TK_AUTO},
    {"",0}, {"",0}, {"",0},
#line 66 "/tmp/keywords.gperf"
    {"_Thread_local", TK_THREAD_LOCAL},
    {"",0}, {"",0}, {"",0}, {"",0},
#line 61 "/tmp/keywords.gperf"
    {"_Complex", TK_COMPLEX},
    {"",0}, {"",0},
#line 38 "/tmp/keywords.gperf"
    {"inline", TK_INLINE},
    {"",0}, {"",0},
#line 54 "/tmp/keywords.gperf"
    {"long", TK_LONG}
  };

const keyword_entry_t *
in_keyword_set (register const char *str, register unsigned int len)
{
  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      unsigned int key = keyword_hash (str, len);

      if (key <= MAX_HASH_VALUE)
        if (len == lengthtable[key])
          {
            register const char *s = wordlist[key].name;

            if (*str == *s && !memcmp (str + 1, s + 1, len - 1))
              return &wordlist[key];
          }
    }
  return 0;
}
#line 67 "/tmp/keywords.gperf"


static token_kind_t lookup_keyword_gperf_impl(const char *s, size_t len) {
  const keyword_entry_t *kw = in_keyword_set(s, len);
  return kw ? kw->kind : TK_EOF;
}

token_kind_t lookup_keyword_gperf(const char *s, int len) {
  return lookup_keyword_gperf_impl(s, (size_t)len);
}
