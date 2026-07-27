#include <assert.h>

#define MAPPED_LOCATION 0010 "macro-mapped.c"
#line MAPPED_LOCATION
_Static_assert(__LINE__ == 10, "leading-zero digit sequence is decimal");
_Static_assert(sizeof(__FILE__) == sizeof("macro-mapped.c"),
               "macro-expanded filename is applied");

#line 20 /* trailing comments are permitted */
_Static_assert(__LINE__ == 20, "plain decimal line is applied");
static const char mapped_file[] = __FILE__;

#define FINAL_LINE 30
#line FINAL_LINE
_Static_assert(__LINE__ == 30, "macro-expanded decimal line is applied");

int main(void) {
  assert(sizeof(mapped_file) == sizeof("macro-mapped.c"));
  assert(mapped_file[0] == 'm');
  assert(mapped_file[5] == '-');
  assert(mapped_file[13] == 'c');
  return 0;
}
