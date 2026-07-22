#include <assert.h>

struct bytes3 {
  unsigned char value[3];
};

union atomic_member_array_union {
  _Atomic(struct bytes3) items[2];
  unsigned char reserve[12];
};

struct atomic_union_holder {
  unsigned char lead;
  union atomic_member_array_union payload;
  unsigned char tail;
};

static union atomic_member_array_union global_initialized = {
    {{{1, 2, 3}}, {{4, 5, 6}}}};
static struct atomic_union_holder global_holder = {
    .lead = 19,
    .payload = {.items = {{{21, 22, 23}}, {{24, 25, 26}}}},
    .tail = 20,
};

_Static_assert(sizeof(union atomic_member_array_union) == 12,
               "union must retain its largest member size");
_Static_assert(_Alignof(union atomic_member_array_union) == 4,
               "union must retain atomic member alignment");
_Static_assert(sizeof(struct atomic_union_holder) == 20,
               "containing struct must retain union storage and tail");

static int representation_matches(
    const union atomic_member_array_union *value,
    unsigned char first, unsigned char second) {
  const unsigned char *bytes = (const unsigned char *)value;
  return bytes[0] == first &&
         bytes[1] == first + 1 &&
         bytes[2] == first + 2 &&
         bytes[4] == second &&
         bytes[5] == second + 1 &&
         bytes[6] == second + 2;
}

static const union atomic_member_array_union *static_initialized(void) {
  static union atomic_member_array_union value = {
      {{{13, 14, 15}}, {{16, 17, 18}}}};
  return &value;
}

static int holder_representation_matches(void) {
  const unsigned char *bytes = (const unsigned char *)&global_holder;
  return bytes[0] == 19 &&
         bytes[4] == 21 && bytes[5] == 22 && bytes[6] == 23 &&
         bytes[8] == 24 && bytes[9] == 25 && bytes[10] == 26 &&
         bytes[16] == 20;
}

int main(void) {
  union atomic_member_array_union local_initialized = {
      {{{7, 8, 9}}, {{10, 11, 12}}}};
  assert(representation_matches(&local_initialized, 7, 10));
  assert(representation_matches(&global_initialized, 1, 4));
  assert(representation_matches(static_initialized(), 13, 16));
  assert(holder_representation_matches());
  return 0;
}
