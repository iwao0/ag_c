/*
 * A file-scope compound literal has static storage duration and can initialize
 * a compatible aggregate subobject.  Preserve all resolved initializer-slot
 * metadata when the value appears inside an array, member, or nested union.
 */
#include <assert.h>

struct Pair {
  int first;
  long second;
};

struct Bytes3 {
  unsigned char values[3];
};

struct Mixed {
  const char *name;
  double weight;
  int *target;
};

union Choice {
  int integer;
  double real;
  struct Pair pair;
};

struct Envelope {
  int tag;
  union Choice choice;
  struct Pair tail;
};

static int anchors[2] = {31, 37};

static struct Pair pairs[2] = {
    (struct Pair){3, 5},
    (struct Pair){7, 11},
};

static _Atomic(struct Pair) atomic_pairs[2] = {
    (struct Pair){13, 17},
    (struct Pair){19, 23},
};

static _Atomic(struct Bytes3) atomic_bytes[2] = {
    (struct Bytes3){{29, 31, 37}},
    (struct Bytes3){{41, 43, 47}},
};

static struct Mixed mixed[2] = {
    (struct Mixed){"left", 1.25, &anchors[0]},
    (struct Mixed){"right", 2.5, &anchors[1]},
};

static union Choice choices[3] = {
    (union Choice){.integer = 41},
    (union Choice){.real = 43.5},
    (union Choice){.pair = {47, 53}},
};

static struct Envelope envelopes[2] = {
    (struct Envelope){
        .tag = 59,
        .choice = {.pair = {61, 67}},
        .tail = {71, 73},
    },
    (struct Envelope){
        .tag = 79,
        .choice = {.real = 83.5},
        .tail = {89, 97},
    },
};

static struct Envelope member_value = {
    .tag = 101,
    .choice = (union Choice){.integer = 103},
    .tail = (struct Pair){107, 109},
};

_Static_assert(sizeof(_Atomic(struct Bytes3)) == 4,
               "three-byte atomic aggregate uses four-byte storage");

int main(void) {
  assert(pairs[0].first == 3);
  assert(pairs[0].second == 5);
  assert(pairs[1].first == 7);
  assert(pairs[1].second == 11);

  struct Pair atomic_first = atomic_pairs[0];
  struct Pair atomic_second = atomic_pairs[1];
  assert(atomic_first.first == 13);
  assert(atomic_first.second == 17);
  assert(atomic_second.first == 19);
  assert(atomic_second.second == 23);

  struct Bytes3 bytes_first = atomic_bytes[0];
  struct Bytes3 bytes_second = atomic_bytes[1];
  assert(bytes_first.values[0] == 29);
  assert(bytes_first.values[1] == 31);
  assert(bytes_first.values[2] == 37);
  assert(bytes_second.values[0] == 41);
  assert(bytes_second.values[1] == 43);
  assert(bytes_second.values[2] == 47);

  assert(mixed[0].name[0] == 'l');
  assert(mixed[0].name[3] == 't');
  assert(mixed[0].weight == 1.25);
  assert(mixed[0].target == &anchors[0]);
  assert(*mixed[0].target == 31);
  assert(mixed[1].name[0] == 'r');
  assert(mixed[1].name[4] == 't');
  assert(mixed[1].weight == 2.5);
  assert(mixed[1].target == &anchors[1]);
  assert(*mixed[1].target == 37);

  assert(choices[0].integer == 41);
  assert(choices[1].real == 43.5);
  assert(choices[2].pair.first == 47);
  assert(choices[2].pair.second == 53);

  assert(envelopes[0].tag == 59);
  assert(envelopes[0].choice.pair.first == 61);
  assert(envelopes[0].choice.pair.second == 67);
  assert(envelopes[0].tail.first == 71);
  assert(envelopes[0].tail.second == 73);
  assert(envelopes[1].tag == 79);
  assert(envelopes[1].choice.real == 83.5);
  assert(envelopes[1].tail.first == 89);
  assert(envelopes[1].tail.second == 97);

  assert(member_value.tag == 101);
  assert(member_value.choice.integer == 103);
  assert(member_value.tail.first == 107);
  assert(member_value.tail.second == 109);
  return 0;
}
