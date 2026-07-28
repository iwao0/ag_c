/*
 * A macro argument is fully expanded before ordinary substitution, but its
 * original preprocessing tokens are used when the parameter is adjacent to
 * # or ##.  The result of token pasting is then rescanned for macro names.
 */
#include <assert.h>
#include <string.h>

#define RAW_CAT(left, right) left ## right
#define CAT(left, right) RAW_CAT(left, right)
#define CAT3(first, second, third) first ## second ## third
#define RAW_STRING(value) #value
#define STRING(value) RAW_STRING(value)

#define PREFIX pre
#define SUFFIX fix
#define VALUE 20
#define BOTH(value) ((value) + value ## suffix)

#define AFTERX(value) X_ ## value
#define XAFTERX(value) AFTERX(value)
#define BUFSIZE TABLESIZE
#define TABLESIZE 1024

#define LEFT RES
#define RIGHT ULT
#define RESULT 42

int main(void) {
  int PREFIXSUFFIX = 7;
  int prefix = 8;
  int VALUEsuffix = 22;
  int X_BUFSIZE = 11;
  int X_1024 = 13;
  int abc = 17;
  int ac = 19;
  int bc = 23;
  int ab = 29;

  assert(RAW_CAT(PREFIX, SUFFIX) == 7);
  assert(CAT(PREFIX, SUFFIX) == 8);
  assert(BOTH(VALUE) == 42);

  assert(AFTERX(BUFSIZE) == 11);
  assert(XAFTERX(BUFSIZE) == 13);

  assert(CAT3(a, b, c) == 17);
  assert(CAT3(a, , c) == 19);
  assert(CAT3(, b, c) == 23);
  assert(CAT3(a, b, ) == 29);

  assert(strcmp(RAW_STRING(VALUE), "VALUE") == 0);
  assert(strcmp(STRING(VALUE), "20") == 0);
  assert(CAT(LEFT, RIGHT) == 42);
  return 0;
}
