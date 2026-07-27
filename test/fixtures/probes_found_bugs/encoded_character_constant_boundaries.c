// C11 6.4.4.4: u/U/L character constants retain their encoded character
// value and have the char16_t/char32_t/wchar_t underlying type.
#define UTF16_HIRAGANA_A u'\u3042'
#define UTF32_GRINNING_FACE U'\U0001F600'
#define WIDE_HIRAGANA_A L'\u3042'

enum encoded_character_values {
  UTF16_ENUM_VALUE = UTF16_HIRAGANA_A,
  UTF32_ENUM_VALUE = UTF32_GRINNING_FACE,
  WIDE_ENUM_VALUE = WIDE_HIRAGANA_A
};

_Static_assert(_Generic('A', int: 1, default: 0) == 1,
               "ordinary character constant type");
_Static_assert(_Generic(u'A', unsigned short: 1, default: 0) == 1,
               "char16_t underlying type");
_Static_assert(_Generic(U'A', unsigned int: 1, default: 0) == 1,
               "char32_t underlying type");
_Static_assert(_Generic(L'A', int: 1, default: 0) == 1,
               "wchar_t underlying type");
_Static_assert(sizeof(u'A') == 2, "char16_t width");
_Static_assert(sizeof(U'A') == 4, "char32_t width");
_Static_assert(sizeof(L'A') == 4, "wchar_t width");
_Static_assert(UTF16_ENUM_VALUE == 0x3042, "UTF-16 UCN value");
_Static_assert(UTF32_ENUM_VALUE == 0x1F600, "UTF-32 UCN value");
_Static_assert(WIDE_ENUM_VALUE == 0x3042, "wide UCN value");
_Static_assert(u'\x3042' == 0x3042, "UTF-16 hex escape value");
_Static_assert(U'\x1F600' == 0x1F600, "UTF-32 hex escape value");
_Static_assert(L'\x3042' == 0x3042, "wide hex escape value");
_Static_assert(u'\777' == 511, "UTF-16 octal escape value");
_Static_assert(u'あ' == 0x3042, "raw UTF-8 to UTF-16 value");
_Static_assert(U'😀' == 0x1F600, "raw UTF-8 to UTF-32 value");
_Static_assert(L'あ' == 0x3042, "raw UTF-8 to wide value");

static unsigned short global_u16 = UTF16_HIRAGANA_A;
static unsigned int global_u32 = UTF32_GRINNING_FACE;
static int global_wide = WIDE_HIRAGANA_A;

int main(void) {
  unsigned short local_u16 = u'\x3042';
  unsigned int local_u32 = U'\x1F600';
  int local_wide = L'あ';

  if (global_u16 != 0x3042 || local_u16 != 0x3042) return 1;
  if (global_u32 != 0x1F600 || local_u32 != 0x1F600) return 2;
  if (global_wide != 0x3042 || local_wide != 0x3042) return 3;
  return 0;
}
