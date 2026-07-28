/*
 * C11 6.10.8.1 requires __STDC_VERSION__ to expand to the long integer
 * constant 201112L, not merely an int-valued token with the same value.
 */
#include <assert.h>
#include <string.h>

#define STRINGIZE_RAW(value) #value
#define STRINGIZE(value) STRINGIZE_RAW(value)
#define PASTE_RAW(left, right) left ## right
#define PASTE(left, right) PASTE_RAW(left, right)

_Static_assert(__STDC_VERSION__ == 201112L,
               "__STDC_VERSION__ value");
_Static_assert(sizeof(__STDC_VERSION__) == sizeof(long),
               "__STDC_VERSION__ has type long");
_Static_assert(
    _Generic(__STDC_VERSION__, long: 1, default: 0),
    "__STDC_VERSION__ generic type");
_Static_assert(
    _Generic(PASTE(__STDC_VERSION__, U), unsigned long: 1, default: 0),
    "__STDC_VERSION__ spelling participates in token paste");

int main(void) {
  assert(strcmp(STRINGIZE(__STDC_VERSION__), "201112L") == 0);
  assert(strcmp(STRINGIZE(PASTE(__STDC_VERSION__, U)), "201112LU") == 0);
  assert(sizeof(STRINGIZE(__STDC_VERSION__)) == sizeof("201112L"));
  return 0;
}
