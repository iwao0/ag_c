/*
 * C11 6.7.9p14-15: each innermost character-array row can be initialized
 * by a compatible string literal, including encoded literals and rows
 * nested in aggregates or higher-dimensional arrays.
 */
#include <assert.h>

static signed char ordinary_rows[2][4] = {"ab", "c"};
static unsigned char utf8_rows[][3] = {u8"hi", u8"x"};
static unsigned short utf16_rows[2][4] = {u"ab", u"c"};
static unsigned int utf32_rows[][3] = {U"hi", U"x"};
static int wide_rows[2][3] = {L"hi", L"x"};
static unsigned short utf16_cube[2][2][3] = {
    {u"ab", u"x"}, {u"yz", u"q"}};

struct encoded_rows {
  unsigned short utf16[2][4];
  unsigned int utf32[2][3];
  int wide[2][3];
};

static struct encoded_rows global_fields = {
    .utf16 = {u"ab", u"c"},
    .utf32 = {U"hi", U"x"},
    .wide = {L"ok", L"q"},
};

int main(void) {
  unsigned short local_utf16[2][4] = {u"ab", u"c"};
  unsigned int local_utf32[][3] = {U"hi", U"x"};
  int local_wide[2][3] = {L"ok", L"q"};
  struct encoded_rows local_fields = {
      .utf16 = {u"xy", u"z"},
      .utf32 = {U"ab", U"c"},
      .wide = {L"hi", L"j"},
  };

  assert(ordinary_rows[0][2] == 0 && ordinary_rows[0][3] == 0);
  assert(ordinary_rows[1][0] == 'c' && ordinary_rows[1][1] == 0);
  assert(sizeof utf8_rows / sizeof utf8_rows[0] == 2);
  assert(utf8_rows[0][2] == 0 && utf8_rows[1][0] == 'x');
  assert(utf16_rows[0][1] == 'b' && utf16_rows[0][2] == 0);
  assert(utf16_rows[1][0] == 'c' && utf16_rows[1][3] == 0);
  assert(sizeof utf32_rows / sizeof utf32_rows[0] == 2);
  assert(utf32_rows[0][1] == 'i' && utf32_rows[1][0] == 'x');
  assert(wide_rows[0][2] == 0 && wide_rows[1][1] == 0);
  assert(utf16_cube[0][0][1] == 'b');
  assert(utf16_cube[0][1][0] == 'x');
  assert(utf16_cube[1][0][1] == 'z');
  assert(utf16_cube[1][1][0] == 'q');
  assert(global_fields.utf16[0][1] == 'b');
  assert(global_fields.utf32[1][0] == 'x');
  assert(global_fields.wide[0][1] == 'k');
  assert(local_utf16[1][0] == 'c' && local_utf16[1][3] == 0);
  assert(local_utf32[0][1] == 'i' && local_utf32[1][2] == 0);
  assert(local_wide[0][1] == 'k' && local_wide[1][2] == 0);
  assert(local_fields.utf16[0][1] == 'y');
  assert(local_fields.utf32[1][0] == 'c');
  assert(local_fields.wide[1][0] == 'j');
  return 0;
}
