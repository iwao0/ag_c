/*
 * C11 6.7.9p14-15: an explicit \0 is part of the encoded string content.
 * Only the final implicit terminator may be omitted by an exact-bound
 * character array.  Preserve that distinction for every encoded width
 * and for aggregate and compound-literal initialization.
 */
#include <assert.h>
#include <uchar.h>
#include <wchar.h>

static char global_utf8[5] = u8"\U0001F600\0";
static char16_t global_utf16[3] = u"\U0001F600\0";
static char32_t global_utf32[2] = U"\U0001F600\0";
static wchar_t global_wide[2] = L"\U0001F600\0";
static char16_t global_rows[2][3] = {
    u"\U0001F600\0",
    u"A\0",
};

struct embedded_strings {
  char utf8[5];
  char16_t utf16[3];
  char32_t utf32[2];
  wchar_t wide[2];
};

static struct embedded_strings global_fields = {
    u8"\U0001F600\0",
    u"\U0001F600\0",
    U"\U0001F600\0",
    L"\U0001F600\0",
};

int main(void) {
  char local_utf8[5] = u8"\U0001F600\0";
  char16_t local_utf16[3] = u"\U0001F600\0";
  static char32_t static_utf32[2] = U"\U0001F600\0";
  struct embedded_strings local_fields = {
      .utf8 = u8"\U0001F600\0",
      .utf16 = u"\U0001F600\0",
      .utf32 = U"\U0001F600\0",
      .wide = L"\U0001F600\0",
  };
  char16_t *compound_utf16 =
      (char16_t[3]){u"\U0001F600\0"};
  char32_t *compound_utf32 =
      (char32_t[2]){U"\U0001F600\0"};

  assert((unsigned char)global_utf8[0] == 0xF0);
  assert((unsigned char)global_utf8[3] == 0x80);
  assert(global_utf8[4] == 0);
  assert(global_utf16[0] == 0xD83D);
  assert(global_utf16[1] == 0xDE00);
  assert(global_utf16[2] == 0);
  assert(global_utf32[0] == 0x1F600);
  assert(global_utf32[1] == 0);
  assert(global_wide[0] == 0x1F600);
  assert(global_wide[1] == 0);
  assert(global_rows[0][2] == 0);
  assert(global_rows[1][0] == 'A');
  assert(global_rows[1][1] == 0);
  assert(global_rows[1][2] == 0);
  assert(global_fields.utf16[2] == 0);
  assert(global_fields.utf32[1] == 0);
  assert((unsigned char)local_utf8[3] == 0x80);
  assert(local_utf8[4] == 0);
  assert(local_utf16[1] == 0xDE00);
  assert(local_utf16[2] == 0);
  assert(static_utf32[0] == 0x1F600);
  assert(static_utf32[1] == 0);
  assert(local_fields.utf16[2] == 0);
  assert(local_fields.wide[1] == 0);
  assert(compound_utf16[1] == 0xDE00);
  assert(compound_utf16[2] == 0);
  assert(compound_utf32[0] == 0x1F600);
  assert(compound_utf32[1] == 0);
  return 0;
}
