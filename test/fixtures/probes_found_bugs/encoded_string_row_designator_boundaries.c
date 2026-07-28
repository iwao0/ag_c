/*
 * C11 6.7.9: array/member designators can select an innermost character
 * row for string initialization, and a following positional initializer
 * continues at the next row.
 */
#include <assert.h>

static unsigned short utf16_rows[3][4] = {
    [2] = u"xy",
    [0] = u"a",
};
static unsigned int utf32_cube[2][2][3] = {
    [1][0] = U"hi",
    [0][1] = U"x",
};
static int wide_continuation[3][3] = {
    [1] = L"ab",
    L"cd",
};

struct encoded_designated {
  unsigned short utf16[3][4];
  unsigned int utf32[2][2][3];
  int wide[3][3];
};

static struct encoded_designated global_fields = {
    .utf16[1] = u"ab",
    .utf32[1][0] = U"hi",
    .wide[2] = L"x",
};

int main(void) {
  unsigned short local_rows[3][4] = {
      [1] = u"ab",
      u"cd",
  };
  struct encoded_designated local_fields = {
      .utf16[2] = u"xy",
      .utf32[0][1] = U"ok",
      .wide[1] = L"q",
  };
  unsigned short (*compound_rows)[4] =
      (unsigned short[3][4]){
          [2] = u"hi",
          [0] = u"x",
      };

  assert(utf16_rows[0][0] == 'a' && utf16_rows[0][1] == 0);
  assert(utf16_rows[1][0] == 0);
  assert(utf16_rows[2][1] == 'y' && utf16_rows[2][2] == 0);
  assert(utf32_cube[0][1][0] == 'x');
  assert(utf32_cube[1][0][1] == 'i');
  assert(utf32_cube[1][1][0] == 0);
  assert(wide_continuation[0][0] == 0);
  assert(wide_continuation[1][1] == 'b');
  assert(wide_continuation[2][1] == 'd');
  assert(global_fields.utf16[1][1] == 'b');
  assert(global_fields.utf32[1][0][1] == 'i');
  assert(global_fields.wide[2][0] == 'x');
  assert(local_rows[0][0] == 0);
  assert(local_rows[1][1] == 'b');
  assert(local_rows[2][1] == 'd');
  assert(local_fields.utf16[2][1] == 'y');
  assert(local_fields.utf32[0][1][1] == 'k');
  assert(local_fields.wide[1][0] == 'q');
  assert(compound_rows[0][0] == 'x');
  assert(compound_rows[1][0] == 0);
  assert(compound_rows[2][1] == 'i');
  return 0;
}
