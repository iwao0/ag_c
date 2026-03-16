#include "keywords.h"
#include <string.h>

typedef struct {
  const char *name;
  token_kind_t kind;
} kw_entry_t;

static token_kind_t lookup_in_bucket(const kw_entry_t *bucket, int n, const char *s, int len) {
  for (int i = 0; i < n; i++) {
    if (strncmp(bucket[i].name, s, len) == 0) return bucket[i].kind;
  }
  return TK_EOF;
}

token_kind_t lookup_keyword(const char *s, int len) {
  char c0 = s[0];
  switch (len) {
    case 2: {
      if (c0 == 'i' && s[1] == 'f') return TK_IF;
      if (c0 == 'd' && s[1] == 'o') return TK_DO;
      return TK_EOF;
    }
    case 3: {
      if (c0 == 'f' && strncmp(s, "for", 3) == 0) return TK_FOR;
      if (c0 == 'i' && strncmp(s, "int", 3) == 0) return TK_INT;
      return TK_EOF;
    }
    case 4: {
      switch (c0) {
        case 'e': if (strncmp(s, "else", 4) == 0) return TK_ELSE;
                  if (strncmp(s, "enum", 4) == 0) return TK_ENUM; break;
        case 'a': if (strncmp(s, "auto", 4) == 0) return TK_AUTO; break;
        case 'c': if (strncmp(s, "case", 4) == 0) return TK_CASE;
                  if (strncmp(s, "char", 4) == 0) return TK_CHAR; break;
        case 'g': if (strncmp(s, "goto", 4) == 0) return TK_GOTO; break;
        case 'l': if (strncmp(s, "long", 4) == 0) return TK_LONG; break;
        case 'v': if (strncmp(s, "void", 4) == 0) return TK_VOID; break;
      }
      return TK_EOF;
    }
    case 5: {
      switch (c0) {
        case 'w': if (strncmp(s, "while", 5) == 0) return TK_WHILE; break;
        case 'b': if (strncmp(s, "break", 5) == 0) return TK_BREAK; break;
        case 'c': if (strncmp(s, "const", 5) == 0) return TK_CONST; break;
        case 'f': if (strncmp(s, "float", 5) == 0) return TK_FLOAT; break;
        case 's': if (strncmp(s, "short", 5) == 0) return TK_SHORT; break;
        case 'u': if (strncmp(s, "union", 5) == 0) return TK_UNION; break;
        case '_': if (strncmp(s, "_Bool", 5) == 0) return TK_BOOL; break;
      }
      return TK_EOF;
    }
    case 6: {
      static const kw_entry_t b[] = {
        {"return", TK_RETURN},
        {"extern", TK_EXTERN},
        {"inline", TK_INLINE},
        {"signed", TK_SIGNED},
        {"sizeof", TK_SIZEOF},
        {"static", TK_STATIC},
        {"struct", TK_STRUCT},
        {"switch", TK_SWITCH},
        {"double", TK_DOUBLE},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 7: {
      static const kw_entry_t b[] = {
        {"default", TK_DEFAULT},
        {"typedef", TK_TYPEDEF},
        {"_Atomic", TK_ATOMIC},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 8: {
      static const kw_entry_t b[] = {
        {"continue", TK_CONTINUE},
        {"register", TK_REGISTER},
        {"restrict", TK_RESTRICT},
        {"unsigned", TK_UNSIGNED},
        {"volatile", TK_VOLATILE},
        {"_Alignas", TK_ALIGNAS},
        {"_Alignof", TK_ALIGNOF},
        {"_Complex", TK_COMPLEX},
        {"_Generic", TK_GENERIC},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 9: {
      static const kw_entry_t b[] = {
        {"_Noreturn", TK_NORETURN},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 10: {
      static const kw_entry_t b[] = {
        {"_Imaginary", TK_IMAGINARY},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 13: {
      static const kw_entry_t b[] = {
        {"_Thread_local", TK_THREAD_LOCAL},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 14: {
      static const kw_entry_t b[] = {
        {"_Static_assert", TK_STATIC_ASSERT},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    default:
      return TK_EOF;
  }
}
