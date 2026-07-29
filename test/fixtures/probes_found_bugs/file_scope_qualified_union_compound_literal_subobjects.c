/*
 * Qualified union compound literals undergo lvalue conversion before they
 * initialize static aggregate subobjects.  Preserve the selected union member
 * through direct, _Generic-selected, cv-qualified, and atomic destinations.
 */
#include <assert.h>

struct Pair {
  int first;
  int second;
};

union CompactChoice {
  unsigned long long bits;
  double real;
  struct Pair pair;
};

struct Reference {
  int *data;
  int (*callback)(int);
  const char *name;
};

union ReferenceChoice {
  struct Reference reference;
  unsigned char raw[sizeof(struct Reference)];
};

static int first_number = 41;
static int second_number = 43;

static int plus_one(int value) {
  return value + 1;
}

static int plus_two(int value) {
  return value + 2;
}

static union CompactChoice direct_choices[3] = {
    (const union CompactChoice){.bits = 0x1122334455667788ULL},
    (volatile union CompactChoice){.real = 3.25},
    (const volatile union CompactChoice){.pair = {5, 7}},
};

static const union CompactChoice constant_choices[2] = {
    (volatile union CompactChoice){.pair = {11, 13}},
    (const union CompactChoice){.bits = 0x8877665544332211ULL},
};

static volatile union CompactChoice volatile_choices[2] = {
    (const union CompactChoice){.real = 17.5},
    (volatile union CompactChoice){.pair = {19, 23}},
};

static _Atomic(union CompactChoice) atomic_choices[3] = {
    (const union CompactChoice){.bits = 0x0123456789abcdefULL},
    (volatile union CompactChoice){.real = 29.5},
    (const volatile union CompactChoice){.pair = {31, 37}},
};

static union CompactChoice generic_choices[2] = {
    _Generic(
        0,
        int: (const union CompactChoice){.pair = {41, 43}},
        default: (volatile union CompactChoice){.pair = {-1, -1}}),
    _Generic(
        0L,
        long: (volatile union CompactChoice){.real = 47.5},
        default: (const union CompactChoice){.real = -1.0}),
};

static union ReferenceChoice reference_choices[2] = {
    (const union ReferenceChoice){
        .reference = {&first_number, plus_one, "first"},
    },
    (volatile union ReferenceChoice){
        .reference = {&second_number, plus_two, "second"},
    },
};

_Static_assert(sizeof(union CompactChoice) == 8,
               "compact union must fit one atomic word");

int main(void) {
  assert(direct_choices[0].bits == 0x1122334455667788ULL);
  assert(direct_choices[1].real == 3.25);
  assert(direct_choices[2].pair.first == 5);
  assert(direct_choices[2].pair.second == 7);

  assert(constant_choices[0].pair.first == 11);
  assert(constant_choices[0].pair.second == 13);
  assert(constant_choices[1].bits == 0x8877665544332211ULL);

  assert(volatile_choices[0].real == 17.5);
  assert(volatile_choices[1].pair.first == 19);
  assert(volatile_choices[1].pair.second == 23);

  union CompactChoice atomic_bits = atomic_choices[0];
  union CompactChoice atomic_real = atomic_choices[1];
  union CompactChoice atomic_pair = atomic_choices[2];
  assert(atomic_bits.bits == 0x0123456789abcdefULL);
  assert(atomic_real.real == 29.5);
  assert(atomic_pair.pair.first == 31);
  assert(atomic_pair.pair.second == 37);

  assert(generic_choices[0].pair.first == 41);
  assert(generic_choices[0].pair.second == 43);
  assert(generic_choices[1].real == 47.5);

  assert(reference_choices[0].reference.data == &first_number);
  assert(*reference_choices[0].reference.data == 41);
  assert(reference_choices[0].reference.callback(10) == 11);
  assert(reference_choices[0].reference.name[0] == 'f');
  assert(reference_choices[0].reference.name[4] == 't');

  assert(reference_choices[1].reference.data == &second_number);
  assert(*reference_choices[1].reference.data == 43);
  assert(reference_choices[1].reference.callback(10) == 12);
  assert(reference_choices[1].reference.name[0] == 's');
  assert(reference_choices[1].reference.name[5] == 'd');
  return 0;
}
