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

#line 40 ""
_Static_assert(__LINE__ == 40, "empty filename line mapping is applied");
static const char empty_mapped_file[] = __FILE__;

#line 50 "restored-mapping.c"
_Static_assert(__LINE__ == 50, "filename mapping can be restored");

int main(void) {
  assert(sizeof(mapped_file) == sizeof("macro-mapped.c"));
  assert(mapped_file[0] == 'm');
  assert(mapped_file[5] == '-');
  assert(mapped_file[13] == 'c');
  assert(sizeof(empty_mapped_file) == 1);
  assert(empty_mapped_file[0] == '\0');
  return 0;
}
