/*
 * C11 6.10.3 permits a macro definition to be repeated when its form,
 * parameters, replacement preprocessing tokens, and inter-token whitespace
 * separation are identical.  Comments count as whitespace; leading and
 * trailing replacement-list whitespace does not participate.
 */
#include <assert.h>
#include <string.h>

#define SAME_OBJECT (17 + 2)
#define SAME_OBJECT (17 /* same separator */ + 2)

#define SAME_FUNCTION(left, right) ((left) + (right))
#define SAME_FUNCTION( left , right ) ((left) /* same separator */ + (right))

#define SAME_EMPTY
#define SAME_EMPTY /* trailing whitespace only */

#define SAME_STRING "line\n"
#define SAME_STRING "line\n"

#define LEADING_TRAILING 23
#define LEADING_TRAILING       23

#define FIRST_TOKEN(value)(value)
#define FIRST_TOKEN(value) (value)

#define REDEFINED_AFTER_UNDEF 29
#undef REDEFINED_AFTER_UNDEF
#define REDEFINED_AFTER_UNDEF 31

int main(void) {
  assert(SAME_OBJECT == 19);
  assert(SAME_FUNCTION(20, 22) == 42);
  assert(strcmp(SAME_STRING, "line\n") == 0);
  assert(LEADING_TRAILING == 23);
  assert(FIRST_TOKEN(37) == 37);
  assert(REDEFINED_AFTER_UNDEF == 31);
  return 0;
}
