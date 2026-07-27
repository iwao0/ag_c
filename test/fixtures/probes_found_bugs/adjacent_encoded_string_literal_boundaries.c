// C11 6.4.5p5: an unprefixed string token adjacent to a prefixed token is
// treated as having that prefix. The prefix may appear before or after the
// unprefixed token, including after macro replacement and an empty token.
#include <uchar.h>
#include <wchar.h>

#define UTF16_HEAD u"A"
#define UTF32_TAIL U"D"
#define WIDE_HEAD L"E"

static const char16_t utf16_global[] = UTF16_HEAD "BC";
static const char32_t utf32_global[] = "ABC" UTF32_TAIL;
static const wchar_t wide_global[] = WIDE_HEAD "FG";
static const char16_t utf16_empty_head[] = u"" "H";
static const char16_t utf16_ucn[] = u"\u3042" "\u3044";
static const char32_t utf32_ucn[] = "\u3042" U"\U0001F600";

_Static_assert(sizeof(UTF16_HEAD "BC") == 4 * sizeof(char16_t),
               "UTF-16 adjacent literal size");
_Static_assert(sizeof("ABC" UTF32_TAIL) == 5 * sizeof(char32_t),
               "UTF-32 adjacent literal size");
_Static_assert(sizeof(WIDE_HEAD "FG") == 4 * sizeof(wchar_t),
               "wide adjacent literal size");
_Static_assert(sizeof(u"" "H") == 2 * sizeof(char16_t),
               "empty prefixed head preserves the sequence prefix");
_Static_assert(sizeof(u"\u3042" "\u3044") == 3 * sizeof(char16_t),
               "unprefixed UCN is converted at the UTF-16 width");
_Static_assert(sizeof("\u3042" U"\U0001F600") == 3 * sizeof(char32_t),
               "unprefixed UCN is converted at the UTF-32 width");

int main(void) {
  const char16_t *u16 = u"ab" "cd";
  const char32_t *u32 = "xy" U"z";
  const wchar_t *wide = L"mn" "op";
  const char *u8 = "qr" u8"st";

  if (utf16_global[0] != 'A' || utf16_global[2] != 'C' ||
      utf16_global[3] != 0) return 1;
  if (utf32_global[0] != 'A' || utf32_global[3] != 'D' ||
      utf32_global[4] != 0) return 2;
  if (wide_global[0] != 'E' || wide_global[2] != 'G' ||
      wide_global[3] != 0) return 3;
  if (utf16_empty_head[0] != 'H' || utf16_empty_head[1] != 0) return 4;
  if (u16[0] != 'a' || u16[3] != 'd' || u16[4] != 0) return 5;
  if (u32[0] != 'x' || u32[2] != 'z' || u32[3] != 0) return 6;
  if (wide[0] != 'm' || wide[3] != 'p' || wide[4] != 0) return 7;
  if (u8[0] != 'q' || u8[3] != 't' || u8[4] != 0) return 8;
  if (utf16_ucn[0] != 0x3042 || utf16_ucn[1] != 0x3044 ||
      utf16_ucn[2] != 0) return 9;
  if (utf32_ucn[0] != 0x3042 || utf32_ucn[1] != 0x1F600 ||
      utf32_ucn[2] != 0) return 10;
  return 0;
}
