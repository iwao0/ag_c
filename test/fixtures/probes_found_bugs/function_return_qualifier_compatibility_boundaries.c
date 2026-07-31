#include <assert.h>

#define CLASSIFY(expression) \
  _Generic((expression), \
           int (*)(void): 1, \
           const int (*)(void): 2, \
           volatile int (*)(void): 3, \
           const volatile int (*)(void): 4, \
           default: 0)

#define CLASSIFY_POINTER_RESULT(expression) \
  _Generic((expression), \
           plain_pointer_result_function *: 1, \
           qualified_pointer_result_function *: 2, \
           pointee_qualified_result_function *: 3, \
           fully_qualified_pointer_result_function *: 4, \
           default: 0)

typedef int plain_result_function(void);
typedef const int qualified_result_function(void);
typedef volatile int volatile_result_function(void);
typedef const volatile int const_volatile_result_function(void);
typedef int *plain_pointer_result_function(void);
typedef int *const qualified_pointer_result_function(void);
typedef const int *pointee_qualified_result_function(void);
typedef const int *const fully_qualified_pointer_result_function(void);

static int pointer_value = 37;

static int plain_result(void) {
  return 17;
}

static const int qualified_result(void) {
  return 42;
}

static volatile int volatile_result(void) {
  return 29;
}

static const volatile int const_volatile_result(void) {
  return 31;
}

static int *plain_pointer_result(void) {
  return &pointer_value;
}

static int *const qualified_pointer_result(void) {
  return &pointer_value;
}

static const int *pointee_qualified_result(void) {
  return &pointer_value;
}

static const int *const fully_qualified_pointer_result(void) {
  return &pointer_value;
}

int main(void) {
  _Static_assert(
      CLASSIFY((plain_result_function *)0) == 1,
      "plain scalar return type remains unqualified");
  _Static_assert(
      CLASSIFY((qualified_result_function *)0) == 2,
      "qualified scalar return type remains distinct");
  _Static_assert(
      CLASSIFY(&plain_result) == 1,
      "plain function designator has the plain return type");
  _Static_assert(
      CLASSIFY(&qualified_result) == 2,
      "qualified function designator has the qualified return type");
  _Static_assert(
      CLASSIFY((volatile_result_function *)0) == 3,
      "volatile scalar return type remains distinct");
  _Static_assert(
      CLASSIFY((const_volatile_result_function *)0) == 4,
      "combined scalar return qualifiers remain distinct");
  _Static_assert(
      CLASSIFY(&volatile_result) == 3,
      "volatile function designator retains its return qualifier");
  _Static_assert(
      CLASSIFY(&const_volatile_result) == 4,
      "combined function return qualifiers remain distinct");
  _Static_assert(
      CLASSIFY_POINTER_RESULT(&plain_pointer_result) == 1,
      "plain pointer return type remains unqualified");
  _Static_assert(
      CLASSIFY_POINTER_RESULT(&qualified_pointer_result) == 2,
      "top-level pointer return qualifier remains distinct");
  _Static_assert(
      CLASSIFY_POINTER_RESULT(&pointee_qualified_result) == 3,
      "pointee qualifier remains distinct from pointer qualifier");
  _Static_assert(
      CLASSIFY_POINTER_RESULT(&fully_qualified_pointer_result) == 4,
      "pointer and pointee return qualifiers are both preserved");

  plain_result_function *plain_pointer = plain_result;
  qualified_result_function *qualified_pointer = qualified_result;
  volatile_result_function *volatile_pointer = volatile_result;
  const_volatile_result_function *const_volatile_pointer =
      const_volatile_result;
  assert(plain_pointer() == 17);
  assert(qualified_pointer() == 42);
  assert(volatile_pointer() == 29);
  assert(const_volatile_pointer() == 31);
  plain_pointer_result_function *plain_pointer_function =
      plain_pointer_result;
  qualified_pointer_result_function *qualified_pointer_function =
      qualified_pointer_result;
  pointee_qualified_result_function *pointee_qualified_pointer_function =
      pointee_qualified_result;
  fully_qualified_pointer_result_function *fully_qualified_pointer_function =
      fully_qualified_pointer_result;
  assert(*plain_pointer_function() == 37);
  assert(*qualified_pointer_function() == 37);
  assert(*pointee_qualified_pointer_function() == 37);
  assert(*fully_qualified_pointer_function() == 37);
  return 0;
}
