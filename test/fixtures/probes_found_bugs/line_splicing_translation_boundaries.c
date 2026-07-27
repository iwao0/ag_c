/*
 * Translation phase 2 removes each backslash-newline pair before tokenization
 * and comment removal.  Splicing may therefore join identifiers, pp-numbers,
 * string contents, macro names, directives, and physical // comment lines.
 */
#include <assert.h>
#include <string.h>

#de\
fine SPLICED_DIRECTIVE 31

#define COMBINE(left, right) ((left) + (right))

static int joined_\
identifier = 17;

// The next physical line remains part of this comment after splicing. \
static int declaration_must_stay_commented = unknown_identifier;

int main(void) {
  const char *joined_string = "alpha\
beta";
  int joined_number = 12\
34;

  assert(joined_\
identifier == 17);
  assert(joined_number == 1234);
  assert(strcmp(joined_string, "alphabeta") == 0);
  assert(COMB\
INE(19, 23) == 42);
  assert(SPLICED_DIRECTIVE == 31);
  return 0;
}
