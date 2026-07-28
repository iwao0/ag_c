/*
 * Preserve the shared public definitions across stdio.h, wchar.h, uchar.h,
 * wctype.h, stdlib.h, stddef.h, and stdint.h when the defining header order is
 * the reverse of the existing wchar/uchar and wchar/stdio coverage.
 */
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
#include <uchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <uchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(_Generic(NULL, void *: 1, default: 0),
               "shared NULL type");
_Static_assert(_Generic(WEOF, wint_t: 1, default: 0),
               "shared WEOF type");
_Static_assert(_Generic(WCHAR_MIN, int: 1, default: 0),
               "shared WCHAR_MIN type");
_Static_assert(_Generic(WCHAR_MAX, int: 1, default: 0),
               "shared WCHAR_MAX type");
_Static_assert(sizeof(FILE *) == sizeof(void *),
               "shared FILE pointer representation");
_Static_assert(sizeof(char16_t) == 2, "shared char16_t representation");
_Static_assert(sizeof(char32_t) == 4, "shared char32_t representation");

#ifdef __wasm32__
_Static_assert(sizeof(mbstate_t) == 32 && _Alignof(mbstate_t) == 4,
               "Wasm mbstate_t representation");
#else
_Static_assert(sizeof(mbstate_t) == 128 && _Alignof(mbstate_t) == 8,
               "native mbstate_t representation");
#endif

static int (*fwide_signature)(FILE *, int) = fwide;
static size_t (*mbrtoc32_signature)(
    char32_t *, const char *, size_t, mbstate_t *) = mbrtoc32;
static wint_t (*towupper_signature)(wint_t) = towupper;
static void *(*malloc_signature)(size_t) = malloc;

int main(void) {
  mbstate_t state = {0};
  FILE *stream = NULL;
  char32_t scalar = U'A';
  wint_t wide = WEOF;

  assert(stream == NULL);
  assert(scalar == (char32_t)'A');
  assert(wide == (wint_t)-1);
  assert(WCHAR_MIN == (-2147483647 - 1));
  assert(WCHAR_MAX == 2147483647);
  assert(sizeof(state) == sizeof(mbstate_t));
  assert(fwide_signature != NULL);
  assert(mbrtoc32_signature != NULL);
  assert(towupper_signature != NULL);
  assert(malloc_signature != NULL);
  return 0;
}
