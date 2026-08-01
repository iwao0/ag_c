/*
 * Encoded and wide string literals retain their array type under sizeof and
 * address-of, and decay to a pointer to the correct element type in ordinary
 * value contexts.  A selected generic association preserves the selected
 * literal's type and value category before the surrounding conversion.
 */
#include <assert.h>
#include <stddef.h>
#include <uchar.h>
#include <wchar.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

_Static_assert(TYPE_IS(u8"ab", char *),
               "UTF-8 string decays to char pointer");
_Static_assert(TYPE_IS(u"ab", char16_t *),
               "UTF-16 string decays to char16_t pointer");
_Static_assert(TYPE_IS(U"ab", char32_t *),
               "UTF-32 string decays to char32_t pointer");
_Static_assert(TYPE_IS(L"ab", wchar_t *),
               "wide string decays to wchar_t pointer");

_Static_assert(TYPE_IS(&u8"ab", char (*)[3]),
               "address of UTF-8 literal is pointer to array");
_Static_assert(TYPE_IS(&u"ab", char16_t (*)[3]),
               "address of UTF-16 literal is pointer to array");
_Static_assert(TYPE_IS(&U"ab", char32_t (*)[3]),
               "address of UTF-32 literal is pointer to array");
_Static_assert(TYPE_IS(&L"ab", wchar_t (*)[3]),
               "address of wide literal is pointer to array");

_Static_assert(sizeof(u8"abc") == 4 * sizeof(char),
               "UTF-8 sizeof includes terminator");
_Static_assert(sizeof(u"abc") == 4 * sizeof(char16_t),
               "UTF-16 sizeof includes terminator");
_Static_assert(sizeof(U"abc") == 4 * sizeof(char32_t),
               "UTF-32 sizeof includes terminator");
_Static_assert(sizeof(L"abc") == 4 * sizeof(wchar_t),
               "wide sizeof includes terminator");

_Static_assert(sizeof(_Generic(0, int: u"abc", default: u"x")) ==
                   4 * sizeof(char16_t),
               "generic-selected UTF-16 literal remains an array for sizeof");
_Static_assert(TYPE_IS(&_Generic(0, int: U"abc", default: U"x"),
                       char32_t (*)[4]),
               "generic-selected UTF-32 literal remains addressable array");
_Static_assert(TYPE_IS(&_Generic(0, int: L"abc", default: L"x"),
                       wchar_t (*)[4]),
               "generic-selected wide literal remains addressable array");

static char *global_utf8 = u8"ab";
static char16_t *global_utf16 = u"cd";
static char32_t *global_utf32 = U"ef";
static wchar_t *global_wide = L"gh";
static char16_t (*global_utf16_array)[3] = &u"ij";
static char32_t (*global_utf32_array)[3] = &U"kl";
static wchar_t (*global_wide_array)[3] = &L"mn";
static char16_t *global_selected_utf16 =
    _Generic(0, int: u"op", default: u"x");

static size_t count_char16(const char16_t *text) {
  size_t length = 0;
  while (text[length] != 0)
    length++;
  return length;
}

static size_t count_char32(const char32_t *text) {
  size_t length = 0;
  while (text[length] != 0)
    length++;
  return length;
}

static size_t count_wide(const wchar_t *text) {
  size_t length = 0;
  while (text[length] != 0)
    length++;
  return length;
}

int main(void) {
  int choose_left = 1;
  char16_t *conditional_utf16 = choose_left ? u"qr" : u"st";
  char32_t *conditional_utf32 = choose_left ? U"uv" : U"wx";
  wchar_t *conditional_wide = choose_left ? L"yz" : L"AB";
  char16_t (*selected_utf16_array)[4] =
      &_Generic(0, int: u"CDE", default: u"x");
  char32_t (*selected_utf32_array)[4] =
      &_Generic(0, int: U"FGH", default: U"x");
  wchar_t (*selected_wide_array)[4] =
      &_Generic(0, int: L"IJK", default: L"x");

  assert(TYPE_IS(choose_left ? u"qr" : u"st", char16_t *));
  assert(TYPE_IS(choose_left ? U"uv" : U"wx", char32_t *));
  assert(TYPE_IS(choose_left ? L"yz" : L"AB", wchar_t *));
  assert(TYPE_IS(_Generic(0, int: u8"LM", default: u8"x"), char *));
  assert(TYPE_IS(_Generic(0, int: u"NO", default: u"x"), char16_t *));
  assert(TYPE_IS(_Generic(0, int: U"PQ", default: U"x"), char32_t *));
  assert(TYPE_IS(_Generic(0, int: L"RS", default: L"x"), wchar_t *));

  assert(global_utf8[0] == 'a' && global_utf8[1] == 'b' &&
         global_utf8[2] == 0);
  assert(count_char16(global_utf16) == 2 && global_utf16[0] == 'c');
  assert(count_char32(global_utf32) == 2 && global_utf32[1] == 'f');
  assert(count_wide(global_wide) == 2 && global_wide[0] == L'g');
  assert((*global_utf16_array)[0] == 'i' &&
         (*global_utf16_array)[2] == 0);
  assert((*global_utf32_array)[1] == 'l' &&
         (*global_utf32_array)[2] == 0);
  assert((*global_wide_array)[0] == L'm' &&
         (*global_wide_array)[2] == 0);
  assert(global_selected_utf16[0] == 'o' &&
         global_selected_utf16[1] == 'p');

  assert(count_char16(conditional_utf16) == 2 &&
         conditional_utf16[0] == 'q');
  assert(count_char32(conditional_utf32) == 2 &&
         conditional_utf32[0] == 'u');
  assert(count_wide(conditional_wide) == 2 &&
         conditional_wide[0] == L'y');
  assert((*selected_utf16_array)[0] == 'C' &&
         (*selected_utf16_array)[3] == 0);
  assert((*selected_utf32_array)[1] == 'G' &&
         (*selected_utf32_array)[3] == 0);
  assert((*selected_wide_array)[2] == L'K' &&
         (*selected_wide_array)[3] == 0);
  return 0;
}
