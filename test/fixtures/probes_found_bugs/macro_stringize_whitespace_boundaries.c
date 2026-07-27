/*
 * Stringizing removes leading/trailing whitespace and replaces each
 * whitespace sequence between preprocessing tokens with one space.  Comments
 * are whitespace before macro replacement, and an empty argument stringizes
 * to the empty string.
 */
#include <assert.h>
#include <string.h>

#define STRINGIZE_RAW(value) #value
#define STRINGIZE(value) STRINGIZE_RAW(value)
#define IGNORE_EMPTY(value) 19
#define PASTE_EMPTY_LEFT(value) value ## empty_marker
#define PASTE_EMPTY_RIGHT(value) empty_marker ## value
#define ZERO_PARAMETER() 23
#define EXPANDED_VALUE 73

int main(void) {
  int empty_marker = 17;

  assert(strcmp(STRINGIZE_RAW(), "") == 0);
  assert(strcmp(STRINGIZE_RAW(   alpha   +   beta   ),
                "alpha + beta") == 0);
  assert(strcmp(STRINGIZE_RAW(alpha /* first */ + /* second */ beta),
                "alpha + beta") == 0);
  assert(strcmp(STRINGIZE_RAW((alpha,    beta)), "(alpha, beta)") == 0);

  assert(strcmp(STRINGIZE_RAW(EXPANDED_VALUE), "EXPANDED_VALUE") == 0);
  assert(strcmp(STRINGIZE(EXPANDED_VALUE), "73") == 0);
  assert(IGNORE_EMPTY() == 19);
  assert(PASTE_EMPTY_LEFT() == 17);
  assert(PASTE_EMPTY_RIGHT() == 17);
  assert(ZERO_PARAMETER() == 23);
  return 0;
}
