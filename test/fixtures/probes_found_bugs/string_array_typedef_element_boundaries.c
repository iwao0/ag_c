/*
 * C11 6.7.9p14-15: typedef aliases preserve character-type compatibility
 * for string array initialization.  This includes standard encoded
 * character typedefs, further aliases, qualified elements, array typedefs,
 * aggregate members, and compound literals.
 */
#include <assert.h>
#include <uchar.h>
#include <wchar.h>

typedef signed char ordinary_char;
typedef unsigned char utf8_char;
typedef char16_t utf16_char;
typedef char32_t utf32_char;
typedef wchar_t wide_char;
typedef utf16_char utf16_alias;
typedef const utf16_alias const_utf16_char;
typedef utf16_char utf16_row[4];

static ordinary_char ordinary[] = "hi";
static utf8_char utf8[] = u8"ok";
static utf16_alias utf16[] = u"hi";
static utf32_char utf32[] = U"ok";
static wide_char wide[] = L"hi";
static utf16_row rows = u"abc";
static const char *global_pointer_array[] = {"one"};
static const char16_t *global_utf16_pointer_array[] = {u"two"};

struct aliases {
  const_utf16_char utf16[4];
  utf32_char utf32[3];
  wide_char wide[3];
};

static struct aliases global_aliases = {
    u"ab",
    U"ok",
    L"hi",
};

int main(void) {
  const_utf16_char local[] = u"xy";
  utf16_row local_row = u"abc";
  const char *local_pointer_array[] = {"three"};
  static const char16_t *static_utf16_pointer_array[] = {u"four"};
  utf16_char (*compound)[4] =
      (utf16_char[2][4]){u"hi", u"x"};
  const char16_t **compound_pointer_array =
      (const char16_t *[1]){u"five"};

  assert(ordinary[0] == 'h' && ordinary[2] == 0);
  assert(utf8[1] == 'k' && utf8[2] == 0);
  assert(utf16[1] == 'i' && utf16[2] == 0);
  assert(utf32[1] == 'k' && utf32[2] == 0);
  assert(wide[1] == 'i' && wide[2] == 0);
  assert(rows[2] == 'c' && rows[3] == 0);
  assert(global_pointer_array[0][1] == 'n');
  assert(global_utf16_pointer_array[0][2] == 'o');
  assert(global_aliases.utf16[1] == 'b');
  assert(global_aliases.utf32[2] == 0);
  assert(global_aliases.wide[1] == 'i');
  assert(local[1] == 'y' && local[2] == 0);
  assert(local_row[2] == 'c' && local_row[3] == 0);
  assert(local_pointer_array[0][4] == 'e');
  assert(static_utf16_pointer_array[0][3] == 'r');
  assert(compound[0][1] == 'i' && compound[0][2] == 0);
  assert(compound[1][0] == 'x' && compound[1][1] == 0);
  assert(compound_pointer_array[0][2] == 'v');
  return 0;
}
