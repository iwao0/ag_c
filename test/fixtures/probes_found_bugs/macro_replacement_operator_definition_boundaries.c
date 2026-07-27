/*
 * C11 6.10.3 replacement-list operator boundaries.
 * In a function-like macro, # must precede a parameter.  In either macro
 * form, ## may not be the first or last replacement token.  A lone # in an
 * unused object-like macro remains an ordinary preprocessing token.
 */
#include <assert.h>
#include <string.h>

#define STRINGIZE(value) #value
#define STRINGIZE_VARIADIC(...) #__VA_ARGS__
#define JOIN(left, right) left##right
#define JOIN_THREE(first, second, third) first##second##third
#define UNSIGNED_LITERAL(value) value##UL
#define OBJECT_JOIN joined_##value
#define UNUSED_OBJECT_HASH # ordinary_token

static int joined_value = 42;
static int joined_name_value = 41;

int main(void) {
  assert(strcmp(STRINGIZE(alpha + beta), "alpha + beta") == 0);
  assert(strcmp(STRINGIZE_VARIADIC(one, two), "one, two") == 0);
  assert(strcmp(STRINGIZE(), "") == 0);
  assert(JOIN(joined_, value) == 42);
  assert(JOIN_THREE(joined_, name_, value) == 41);
  assert(UNSIGNED_LITERAL(42) == 42UL);
  assert(OBJECT_JOIN == 42);
  return 0;
}
