/*
 * Preserve the target ABI identities of the wide-character descriptor
 * types, not only the signatures spelled through their typedef names.
 */
#include <assert.h>
#include <wctype.h>

_Static_assert(_Generic((wint_t)0, int: 1, default: 0),
               "wint_t target identity");
_Static_assert(_Generic((wctrans_t)0, int: 1, default: 0),
               "wctrans_t target identity");
#ifdef __wasm32__
_Static_assert(_Generic((wctype_t)0, int: 1, default: 0),
               "Wasm wctype_t runtime identity");
_Static_assert((wctype_t)-1 < 0, "Wasm wctype_t signedness");
#else
_Static_assert(_Generic((wctype_t)0, unsigned int: 1, default: 0),
               "Apple arm64 wctype_t ABI identity");
_Static_assert((wctype_t)-1 > 0, "native wctype_t unsignedness");
#endif
_Static_assert(sizeof(wint_t) == 4 && _Alignof(wint_t) == 4,
               "wint_t representation");
_Static_assert(sizeof(wctype_t) == 4 && _Alignof(wctype_t) == 4,
               "wctype_t representation");
_Static_assert(sizeof(wctrans_t) == 4 && _Alignof(wctrans_t) == 4,
               "wctrans_t representation");

static wctype_t (*wctype_signature)(const char *) = wctype;
static int (*iswctype_signature)(wint_t, wctype_t) = iswctype;
static wctrans_t (*wctrans_signature)(const char *) = wctrans;
static wint_t (*towctrans_signature)(wint_t, wctrans_t) = towctrans;

#ifdef __wasm32__
static int (*wctype_underlying_signature)(const char *) = wctype;
static int (*iswctype_underlying_signature)(int, int) = iswctype;
#else
static unsigned int (*wctype_underlying_signature)(const char *) = wctype;
static int (*iswctype_underlying_signature)(
    int, unsigned int) = iswctype;
#endif
static int (*wctrans_underlying_signature)(const char *) = wctrans;
static int (*towctrans_underlying_signature)(int, int) = towctrans;

int main(void) {
  wctype_t alpha = wctype_signature("alpha");
  wctype_t digit = wctype_underlying_signature("digit");
  wctrans_t lower = wctrans_signature("tolower");
  wctrans_t upper = wctrans_underlying_signature("toupper");

  assert(alpha != (wctype_t)0);
  assert(digit != (wctype_t)0);
  assert(iswctype_signature(L'A', alpha) != 0);
  assert(iswctype_underlying_signature(L'7', digit) != 0);
  assert(iswctype_signature(L'7', alpha) == 0);
  assert(wctype_signature("ag_c_missing") == (wctype_t)0);
  assert(lower != (wctrans_t)0);
  assert(upper != (wctrans_t)0);
  assert(towctrans_signature(L'Q', lower) == L'q');
  assert(towctrans_underlying_signature(L'q', upper) == L'Q');
  assert(wctrans_signature("ag_c_missing") == (wctrans_t)0);
  return 0;
}
