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
  switch (len) {
    case 2: {
      static const kw_entry_t b[] = {
        {"if", TK_IF},
        {"do", TK_DO},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 3: {
      static const kw_entry_t b[] = {
        {"for", TK_FOR},
        {"int", TK_INT},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 4: {
      static const kw_entry_t b[] = {
        {"else", TK_ELSE},
        {"auto", TK_AUTO},
        {"case", TK_CASE},
        {"enum", TK_ENUM},
        {"goto", TK_GOTO},
        {"long", TK_LONG},
        {"char", TK_CHAR},
        {"void", TK_VOID},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
    }
    case 5: {
      static const kw_entry_t b[] = {
        {"while", TK_WHILE},
        {"break", TK_BREAK},
        {"const", TK_CONST},
        {"float", TK_FLOAT},
        {"short", TK_SHORT},
        {"_Bool", TK_BOOL},
        {"union", TK_UNION},
      };
      return lookup_in_bucket(b, (int)(sizeof(b) / sizeof(b[0])), s, len);
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

