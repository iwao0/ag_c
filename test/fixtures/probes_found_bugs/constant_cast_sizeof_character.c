/* Integer constant-expression coverage for casts, sizeof, and characters. */
#include <assert.h>
#include <stddef.h>

enum ConstantCastValues {
  CAST_UNSIGNED_CHAR_WRAP = (unsigned char)257,
  CAST_UNSIGNED_SHORT_WRAP = (unsigned short)65537,
  CAST_BOOL_FALSE = (_Bool)0,
  CAST_BOOL_TRUE = (_Bool)-27,
  SIZEOF_UNSIGNED_WRAP = sizeof(int) - 5 > 0,
  CHARACTER_ESCAPE_VALUE = '\n'
};

_Static_assert((unsigned char)256 == 0,
               "unsigned char cast truncates");
_Static_assert((unsigned short)65537 == 1,
               "unsigned short cast truncates");
_Static_assert((_Bool)-27 == 1, "_Bool cast normalizes");
_Static_assert(sizeof(int) - 5 > 0,
               "sizeof arithmetic uses unsigned size_t");
_Static_assert(sizeof(int) - 5 == (size_t)-1,
               "sizeof subtraction wraps at size_t width");
_Static_assert(sizeof 'A' == sizeof(int),
               "ordinary character constants have type int");
_Static_assert('A' + '\n' + '\0' == 75,
               "character constants are integer constants");

int cast_bound[(unsigned char)257 == 1 ? 3 : -1];
int sizeof_bound[sizeof(int) - 5 > 0 ? 2 : -1];

struct ConstantWidths {
  unsigned cast_width : (unsigned char)3;
  unsigned character_width : ('\n' == 10 ? 4 : -1);
};

static unsigned char truncated_byte = (unsigned char)257;
static unsigned short truncated_short = (unsigned short)65537;
static _Bool normalized_bool = (_Bool)-27;
static size_t wrapped_size = sizeof(int) - 5;
static int character_sum = 'A' + '\n' + '\0';

int main(void) {
  struct ConstantWidths widths = {7, 15};
  assert(CAST_UNSIGNED_CHAR_WRAP == 1);
  assert(CAST_UNSIGNED_SHORT_WRAP == 1);
  assert(CAST_BOOL_FALSE == 0);
  assert(CAST_BOOL_TRUE == 1);
  assert(SIZEOF_UNSIGNED_WRAP == 1);
  assert(CHARACTER_ESCAPE_VALUE == 10);
  assert(sizeof(cast_bound) == 3 * sizeof(int));
  assert(sizeof(sizeof_bound) == 2 * sizeof(int));
  assert(widths.cast_width == 7);
  assert(widths.character_width == 15);
  assert(truncated_byte == 1);
  assert(truncated_short == 1);
  assert(normalized_bool == 1);
  assert(wrapped_size == (size_t)-1);
  assert(character_sum == 75);
  return 0;
}
