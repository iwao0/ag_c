/*
 * C11 6.7.9p14-15: a character array may omit only the implicit
 * terminating null when its explicit bound exactly fits the encoded
 * content.  The bound is measured in UTF-8 bytes, UTF-16 code units, or
 * UTF-32/wide code units according to the string prefix.
 */
#include <assert.h>
#include <uchar.h>
#include <wchar.h>

static char global_utf8_exact[4] = u8"\U0001F600";
static char16_t global_utf16_exact[2] = u"\U0001F600";
static char32_t global_utf32_exact[1] = U"\U0001F600";
static wchar_t global_wide_exact[1] = L"\U0001F600";
static char16_t global_rows[2][2] = {
    u"\U0001F600",
    u"\u3042",
};

struct exact_strings {
  char utf8[4];
  char16_t utf16[2];
  char32_t utf32[1];
  wchar_t wide[1];
};

static struct exact_strings global_fields = {
    u8"\U0001F600",
    u"\U0001F600",
    U"\U0001F600",
    L"\U0001F600",
};

int main(void) {
  char local_utf8[4] = u8"\U0001F600";
  char16_t local_utf16[2] = u"\U0001F600";
  static char32_t static_utf32[1] = U"\U0001F600";
  struct exact_strings local_fields = {
      .utf8 = u8"\U0001F600",
      .utf16 = u"\U0001F600",
      .utf32 = U"\U0001F600",
      .wide = L"\U0001F600",
  };
  char16_t *compound_utf16 =
      (char16_t[2]){u"\U0001F600"};
  char32_t *compound_utf32 =
      (char32_t[1]){U"\U0001F600"};

  assert((unsigned char)global_utf8_exact[0] == 0xF0);
  assert((unsigned char)global_utf8_exact[3] == 0x80);
  assert(global_utf16_exact[0] == 0xD83D);
  assert(global_utf16_exact[1] == 0xDE00);
  assert(global_utf32_exact[0] == 0x1F600);
  assert(global_wide_exact[0] == 0x1F600);
  assert(global_rows[0][0] == 0xD83D);
  assert(global_rows[0][1] == 0xDE00);
  assert(global_rows[1][0] == 0x3042);
  assert(global_rows[1][1] == 0);
  assert(global_fields.utf16[1] == 0xDE00);
  assert(global_fields.utf32[0] == 0x1F600);
  assert((unsigned char)local_utf8[3] == 0x80);
  assert(local_utf16[0] == 0xD83D);
  assert(local_utf16[1] == 0xDE00);
  assert(static_utf32[0] == 0x1F600);
  assert(local_fields.utf16[1] == 0xDE00);
  assert(local_fields.wide[0] == 0x1F600);
  assert(compound_utf16[0] == 0xD83D);
  assert(compound_utf16[1] == 0xDE00);
  assert(compound_utf32[0] == 0x1F600);
  return 0;
}
