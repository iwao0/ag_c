#include <assert.h>
#include <stddef.h>

struct bytes3 {
  unsigned char value[3];
};

struct atomic_member_array {
  unsigned char lead;
  _Atomic(struct bytes3) items[2];
  unsigned char tail;
};

static struct atomic_member_array global_initialized = {
    17, {{{1, 2, 3}}, {{4, 5, 6}}}, 18};

_Static_assert(sizeof(_Atomic(struct bytes3)) == 4,
               "atomic member element storage");
_Static_assert(sizeof(struct atomic_member_array) == 16,
               "atomic member array containing record size");

static int representation_matches(const struct atomic_member_array *value,
                                  unsigned char first,
                                  unsigned char second) {
  const unsigned char *bytes = (const unsigned char *)value;
  return bytes[0] == 17 &&
         bytes[4] == first &&
         bytes[5] == first + 1 &&
         bytes[6] == first + 2 &&
         bytes[8] == second &&
         bytes[9] == second + 1 &&
         bytes[10] == second + 2 &&
         bytes[12] == 18;
}

static const struct atomic_member_array *static_initialized(void) {
  static struct atomic_member_array value = {
      17, {{{13, 14, 15}}, {{16, 17, 18}}}, 18};
  return &value;
}

int main(void) {
  struct atomic_member_array local_initialized = {
      17, {{{7, 8, 9}}, {{10, 11, 12}}}, 18};
  assert(offsetof(struct atomic_member_array, items) == 4);
  assert(offsetof(struct atomic_member_array, tail) == 12);
  assert(representation_matches(&global_initialized, 1, 4));
  assert(representation_matches(&local_initialized, 7, 10));
  assert(representation_matches(static_initialized(), 13, 16));
  return 0;
}
