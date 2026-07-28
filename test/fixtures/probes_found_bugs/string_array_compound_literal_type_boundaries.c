/*
 * C11 6.5.2.5 and 6.7.9p14-15: character-array compound literals use
 * the same prefix/element-type and terminating-null rules as declarations.
 */
#include <assert.h>

static const signed char *file_ordinary =
    (const signed char[]){"hi"};
static const unsigned char *file_utf8 =
    (const unsigned char[2]){u8"ok"};
static const unsigned short *file_utf16 =
    (const unsigned short[]){u"hi"};
static const unsigned int *file_utf32 =
    (const unsigned int[2]){U"ok"};
static const int *file_wide =
    (const int[]){L"wide"};

int main(void) {
  const signed char *local_ordinary =
      (const signed char[2]){"hi"};
  const unsigned char *local_utf8 =
      (const unsigned char[]){u8"ok"};
  const unsigned short *local_utf16 =
      (const unsigned short[2]){u"hi"};
  const unsigned int *local_utf32 =
      (const unsigned int[]){U"ok"};
  const int *local_wide =
      (const int[2]){L"hi"};

  assert(file_ordinary[0] == 'h' && file_ordinary[2] == 0);
  assert(file_utf8[0] == 'o' && file_utf8[1] == 'k');
  assert(file_utf16[0] == 'h' && file_utf16[2] == 0);
  assert(file_utf32[0] == 'o' && file_utf32[1] == 'k');
  assert(file_wide[0] == 'w' && file_wide[4] == 0);

  assert(local_ordinary[0] == 'h' && local_ordinary[1] == 'i');
  assert(local_utf8[0] == 'o' && local_utf8[2] == 0);
  assert(local_utf16[0] == 'h' && local_utf16[1] == 'i');
  assert(local_utf32[0] == 'o' && local_utf32[2] == 0);
  assert(local_wide[0] == 'h' && local_wide[1] == 'i');
  return 0;
}
