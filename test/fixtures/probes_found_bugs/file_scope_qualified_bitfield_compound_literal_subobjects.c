/*
 * A file-scope compound literal may initialize a compatible aggregate
 * subobject even when the source or destination has top-level qualifiers.
 * Copying the value must preserve packed bit-field slots and relocations
 * selected through anonymous union members.
 */
#include <assert.h>

typedef struct PackedValue {
  unsigned char prefix[3];
  unsigned int head : 5;
  unsigned int middle : 6;
  unsigned int wide : 26;
  unsigned char tail;
} PackedValue;

struct QualifiedHolder {
  int before;
  const PackedValue value;
  int after;
};

struct AnonymousReference {
  unsigned int lead : 3;
  union {
    struct {
      int *data;
      int (*callback)(int);
    };
    unsigned long long raw[2];
  };
  unsigned int trail : 5;
};

struct ReferenceEnvelope {
  int tag;
  struct AnonymousReference reference;
  const char *name;
};

static int first_number = 41;
static int second_number = 43;

static int plus_one(int value) {
  return value + 1;
}

static int plus_two(int value) {
  return value + 2;
}

static PackedValue packed_values[2] = {
    (const PackedValue){
        .prefix = {3, 5, 7},
        .head = 11,
        .middle = 13,
        .wide = 0x1234567,
        .tail = 17,
    },
    (volatile PackedValue){
        .prefix = {19, 23, 29},
        .head = 31,
        .middle = 37,
        .wide = 0x2abcdef,
        .tail = 41,
    },
};

static struct QualifiedHolder qualified_holders[2] = {
    (struct QualifiedHolder){
        .before = 43,
        .value = {
            .prefix = {47, 53, 59},
            .head = 17,
            .middle = 23,
            .wide = 0x3456789,
            .tail = 61,
        },
        .after = 67,
    },
    (const struct QualifiedHolder){
        .before = 71,
        .value = {
            .prefix = {73, 79, 83},
            .head = 19,
            .middle = 29,
            .wide = 0x2345678,
            .tail = 89,
        },
        .after = 97,
    },
};

static struct ReferenceEnvelope references[2] = {
    (struct ReferenceEnvelope){
        .tag = 101,
        .reference = {
            .lead = 5,
            .data = &first_number,
            .callback = plus_one,
            .trail = 17,
        },
        .name = "first",
    },
    (const struct ReferenceEnvelope){
        .tag = 103,
        .reference = {
            .lead = 3,
            .data = &second_number,
            .callback = plus_two,
            .trail = 19,
        },
        .name = "second",
    },
};

static int packed_matches(
    const PackedValue *value,
    unsigned char first, unsigned char second, unsigned char third,
    int head, int middle, int wide,
    unsigned char tail) {
  return value->prefix[0] == first &&
         value->prefix[1] == second &&
         value->prefix[2] == third &&
         value->head == head &&
         value->middle == middle &&
         value->wide == wide &&
         value->tail == tail;
}

int main(void) {
  assert(packed_matches(
      &packed_values[0], 3, 5, 7, 11, 13, 0x1234567, 17));
  assert(packed_matches(
      &packed_values[1], 19, 23, 29, 31, 37, 0x2abcdef, 41));

  assert(qualified_holders[0].before == 43);
  assert(packed_matches(
      &qualified_holders[0].value,
      47, 53, 59, 17, 23, 0x3456789, 61));
  assert(qualified_holders[0].after == 67);
  assert(qualified_holders[1].before == 71);
  assert(packed_matches(
      &qualified_holders[1].value,
      73, 79, 83, 19, 29, 0x2345678, 89));
  assert(qualified_holders[1].after == 97);

  assert(references[0].tag == 101);
  assert(references[0].reference.lead == 5);
  assert(references[0].reference.data == &first_number);
  assert(*references[0].reference.data == 41);
  assert(references[0].reference.callback(10) == 11);
  assert(references[0].reference.trail == 17);
  assert(references[0].name[0] == 'f');
  assert(references[0].name[4] == 't');

  assert(references[1].tag == 103);
  assert(references[1].reference.lead == 3);
  assert(references[1].reference.data == &second_number);
  assert(*references[1].reference.data == 43);
  assert(references[1].reference.callback(10) == 12);
  assert(references[1].reference.trail == 19);
  assert(references[1].name[0] == 's');
  assert(references[1].name[5] == 'd');
  return 0;
}
