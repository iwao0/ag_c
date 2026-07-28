/*
 * C11 6.4.5 and 6.7.9p14-15: incomplete character arrays infer their
 * element count from encoded code units, not source UTF-8 bytes.  Typedef
 * aliases and the direct/braced initializer spellings must preserve this
 * rule in global, automatic, static-local, and compound-literal storage.
 */
#include <assert.h>
#include <uchar.h>
#include <wchar.h>

typedef char16_t utf16_char;
typedef char32_t utf32_char;
typedef wchar_t wide_char;
typedef utf16_char utf16_alias;

static utf16_alias global_utf16_bmp[] = u"\u3042";
static utf16_alias global_utf16_supplementary[] = u"\U0001F600";
static utf16_alias global_utf16_braced[] = {u"\U0001F600"};
static utf32_char global_utf32_supplementary[] = U"\U0001F600";
static wide_char global_wide_supplementary[] = L"\U0001F600";

_Static_assert(sizeof(global_utf16_bmp) == 2 * sizeof(utf16_alias),
               "BMP uses one UTF-16 code unit plus null");
_Static_assert(sizeof(global_utf16_supplementary) ==
                   3 * sizeof(utf16_alias),
               "supplementary scalar uses a surrogate pair plus null");
_Static_assert(sizeof(global_utf32_supplementary) ==
                   2 * sizeof(utf32_char),
               "supplementary scalar uses one UTF-32 code unit plus null");
_Static_assert(sizeof(global_wide_supplementary) ==
                   2 * sizeof(wide_char),
               "supplementary scalar uses one wide code unit plus null");

int main(void) {
  utf16_alias local_mixed[] = u"A\U0001F600B";
  utf16_alias local_braced[] = {u"\U0001F600"};
  static utf16_alias static_local_supplementary[] = u"\U0001F600";
  static utf16_alias static_local_braced[] = {u"\U0001F600"};
  utf16_alias *compound_utf16 =
      (utf16_alias[]){u"\U0001F600"};
  utf32_char *compound_utf32 =
      (utf32_char[]){U"\U0001F600"};

  assert(sizeof(local_mixed) == 5 * sizeof(utf16_alias));
  assert(local_mixed[0] == 'A');
  assert(local_mixed[1] == 0xD83D);
  assert(local_mixed[2] == 0xDE00);
  assert(local_mixed[3] == 'B');
  assert(local_mixed[4] == 0);
  assert(sizeof(static_local_supplementary) ==
         3 * sizeof(utf16_alias));
  assert(global_utf16_bmp[0] == 0x3042 &&
         global_utf16_bmp[1] == 0);
  assert(global_utf16_supplementary[0] == 0xD83D &&
         global_utf16_supplementary[1] == 0xDE00 &&
         global_utf16_supplementary[2] == 0);
  assert(sizeof(global_utf16_braced) ==
         3 * sizeof(utf16_alias));
  assert(global_utf16_braced[0] == 0xD83D &&
         global_utf16_braced[1] == 0xDE00 &&
         global_utf16_braced[2] == 0);
  assert(global_utf32_supplementary[0] == 0x1F600 &&
         global_utf32_supplementary[1] == 0);
  assert(global_wide_supplementary[0] == 0x1F600 &&
         global_wide_supplementary[1] == 0);
  assert(static_local_supplementary[0] == 0xD83D &&
         static_local_supplementary[1] == 0xDE00 &&
         static_local_supplementary[2] == 0);
  assert(sizeof(local_braced) == 3 * sizeof(utf16_alias));
  assert(local_braced[0] == 0xD83D &&
         local_braced[1] == 0xDE00 &&
         local_braced[2] == 0);
  assert(sizeof(static_local_braced) ==
         3 * sizeof(utf16_alias));
  assert(static_local_braced[0] == 0xD83D &&
         static_local_braced[1] == 0xDE00 &&
         static_local_braced[2] == 0);
  assert(compound_utf16[0] == 0xD83D &&
         compound_utf16[1] == 0xDE00 &&
         compound_utf16[2] == 0);
  assert(compound_utf32[0] == 0x1F600 &&
         compound_utf32[1] == 0);
  return 0;
}
