/*
 * Preserve the complete C11 <stdalign.h> macro surface with expression- and
 * type-name alignments on objects and members.
 */
#include <assert.h>
#include <stdalign.h>
#include <stdalign.h>

#if __alignas_is_defined != 1
#error "alignas macro availability must be advertised"
#endif
#if __alignof_is_defined != 1
#error "alignof macro availability must be advertised"
#endif

typedef int int_array[4];

struct aligned_member {
  char prefix;
  alignas(32) int value;
  char suffix;
};

static alignas(64) unsigned char aligned_global = 7;
alignas(long long) static int type_aligned_global = 11;
alignas(0) static int zero_aligned_global = 13;

_Static_assert(alignof(char) == 1, "char alignment");
_Static_assert(alignof(int_array) == alignof(int), "array alignment");
_Static_assert(alignof(struct aligned_member) >= 32,
               "member alignment propagates to the record");

static int check_local_objects(void) {
  alignas(64) unsigned char local_bytes[3] = {2, 3, 5};
  alignas(long long) int type_aligned_local = 17;
  struct aligned_member value = {'a', 19, 'z'};

  assert((unsigned long)&local_bytes[0] % 64 == 0);
  assert((unsigned long)&type_aligned_local % alignof(long long) == 0);
  assert((unsigned long)&value % alignof(struct aligned_member) == 0);
  assert((unsigned long)&value.value % 32 == 0);
  assert(local_bytes[0] + local_bytes[1] + local_bytes[2] == 10);
  assert(type_aligned_local == 17);
  assert(value.prefix == 'a' && value.value == 19 && value.suffix == 'z');
  return 0;
}

int main(void) {
  assert((unsigned long)&aligned_global % 64 == 0);
  assert((unsigned long)&type_aligned_global % alignof(long long) == 0);
  assert(aligned_global == 7);
  assert(type_aligned_global == 11);
  assert(zero_aligned_global == 13);
  assert(check_local_objects() == 0);
  return 0;
}
