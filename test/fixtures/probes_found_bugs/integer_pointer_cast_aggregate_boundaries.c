/*
 * Preserve target-width integer-to-pointer casts in static aggregate data and
 * comparison expressions.  Signed narrow constants sign-extend; unsigned
 * constants zero-extend when the target pointer is wider.
 */
#include <assert.h>

typedef int (*function_pointer)(void);

struct pointer_sentinels {
  void *signed_data;
  void *unsigned_data;
  function_pointer signed_function;
  function_pointer unsigned_function;
};

static void *global_data[] = {
    (void *)-1,
    (void *)0x80000000U,
};
static function_pointer global_functions[] = {
    (function_pointer)-1,
    (function_pointer)0x80000000U,
};
static struct pointer_sentinels global_record = {
    (void *)-1,
    (void *)0x80000000U,
    (function_pointer)-1,
    (function_pointer)0x80000000U,
};

static int check_static_local(void) {
  static struct pointer_sentinels value = {
      (void *)-1,
      (void *)0x80000000U,
      (function_pointer)-1,
      (function_pointer)0x80000000U,
  };

  assert(value.signed_data == (void *)-1);
  assert(value.unsigned_data == (void *)0x80000000U);
  assert(value.signed_function == (function_pointer)-1);
  assert(value.unsigned_function == (function_pointer)0x80000000U);
  return 0;
}

int main(void) {
  assert(global_data[0] == (void *)-1);
  assert(global_data[1] == (void *)0x80000000U);
  assert(global_functions[0] == (function_pointer)-1);
  assert(global_functions[1] == (function_pointer)0x80000000U);
  assert(global_record.signed_data == (void *)-1);
  assert(global_record.unsigned_data == (void *)0x80000000U);
  assert(global_record.signed_function == (function_pointer)-1);
  assert(global_record.unsigned_function ==
         (function_pointer)0x80000000U);
  assert(check_static_local() == 0);
  return 0;
}
