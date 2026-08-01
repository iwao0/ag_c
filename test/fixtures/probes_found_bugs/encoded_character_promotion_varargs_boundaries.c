/*
 * Encoded character constants have distinct underlying integer types.
 * Integer promotions and default argument promotions must use those types:
 * char16_t promotes to int, char32_t remains unsigned int, and wchar_t is int
 * for the supported targets.
 */
#include <assert.h>
#include <stdarg.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

_Static_assert(TYPE_IS(+u'A', int),
               "unary plus promotes char16_t to int");
_Static_assert(TYPE_IS(+U'A', unsigned int),
               "unary plus preserves char32_t unsigned int");
_Static_assert(TYPE_IS(+L'A', int),
               "unary plus preserves wchar_t int");
_Static_assert(TYPE_IS(u'A' + 1, int),
               "char16_t participates in arithmetic as int");
_Static_assert(TYPE_IS(U'A' + 1, unsigned int),
               "char32_t arithmetic uses unsigned int");
_Static_assert(TYPE_IS(L'A' + 1, int),
               "wchar_t arithmetic uses int");
_Static_assert(TYPE_IS(1 ? u'A' : U'B', unsigned int),
               "mixed char16_t/char32_t conditional converts to unsigned int");
_Static_assert(TYPE_IS(1 ? u'A' : L'B', int),
               "mixed char16_t/wchar_t conditional converts to int");

static int check_encoded_arguments(int expected_count, ...) {
  va_list arguments;
  va_start(arguments, expected_count);

  int ordinary = va_arg(arguments, int);
  int utf16_ascii = va_arg(arguments, int);
  int utf16_max = va_arg(arguments, int);
  unsigned int utf32_scalar = va_arg(arguments, unsigned int);
  unsigned int utf32_high = va_arg(arguments, unsigned int);
  int wide_value = va_arg(arguments, int);

  va_end(arguments);
  assert(expected_count == 6);
  assert(ordinary == 'A');
  assert(utf16_ascii == 0x3042);
  assert(utf16_max == 0xFFFF);
  assert(utf32_scalar == 0x1F600U);
  assert(utf32_high == 0x80000000U);
  assert(wide_value == 0x3042);
  return 1;
}

typedef int encoded_checker_t(int, ...);

static int check_promoted_objects(int expected_count, ...) {
  va_list arguments;
  va_start(arguments, expected_count);

  int utf16_value = va_arg(arguments, int);
  unsigned int utf32_value = va_arg(arguments, unsigned int);
  int wide_value = va_arg(arguments, int);

  va_end(arguments);
  assert(expected_count == 3);
  assert(utf16_value == 0x3042);
  assert(utf32_value == 0x1F600U);
  assert(wide_value == 0x3042);
  return 1;
}

int main(void) {
  unsigned short utf16_value = u'\u3042';
  unsigned int utf32_value = U'\U0001F600';
  int wide_value = L'\u3042';
  volatile unsigned short volatile_utf16 = u'\u3042';
  volatile unsigned int volatile_utf32 = U'\U0001F600';
  volatile int volatile_wide = L'\u3042';

  assert(check_encoded_arguments(
      6, 'A', u'\u3042', u'\xFFFF',
      U'\U0001F600', U'\x80000000', L'\u3042'));

  encoded_checker_t *checker = check_promoted_objects;
  assert(checker(3, utf16_value, utf32_value, wide_value));
  assert(checker(3, volatile_utf16, volatile_utf32, volatile_wide));

  assert(TYPE_IS(utf16_value + 0, int));
  assert(TYPE_IS(utf32_value + 0, unsigned int));
  assert(TYPE_IS(wide_value + 0, int));
  assert((1 ? utf16_value : utf32_value) == 0x3042U);
  assert((0 ? utf16_value : utf32_value) == 0x1F600U);
  return 0;
}
