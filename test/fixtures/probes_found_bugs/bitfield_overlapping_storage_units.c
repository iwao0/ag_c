#include <assert.h>

/*
 * head shares the four-byte allocation unit that contains prefix[0..2].
 * middle and wide fill the following unit. Static data lowering must merge
 * the first packed unit without erasing prefix and must advance to the second
 * packed unit without reusing the colliding prefix slot.
 */
struct overlapping_bitfields {
  unsigned char prefix[3];
  unsigned int head : 5;
  unsigned int middle : 6;
  unsigned int wide : 26;
  unsigned char tail;
};

static struct overlapping_bitfields global_value = {
    .prefix = {11, 22, 33},
    .head = 17,
    .middle = 45,
    .wide = 0x2abcdef,
    .tail = 44,
};

static struct overlapping_bitfields global_values[] = {
    [2] = {
        .prefix = {55, 66, 77},
        .head = 19,
        .middle = 47,
        .wide = 0x1234567,
        .tail = 88,
    },
};

static int value_matches(
    const struct overlapping_bitfields *value,
    unsigned char first, unsigned char second, unsigned char third,
    unsigned int head, unsigned int middle, unsigned int wide,
    unsigned char tail) {
  return value->prefix[0] == first &&
         value->prefix[1] == second &&
         value->prefix[2] == third &&
         value->head == head &&
         value->middle == middle &&
         value->wide == wide &&
         value->tail == tail;
}

static int check_global_objects(void) {
  assert(value_matches(
      &global_value, 11, 22, 33, 17, 45, 0x2abcdef, 44));

  assert(sizeof(global_values) / sizeof(global_values[0]) == 3);
  assert(value_matches(
      &global_values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &global_values[1], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &global_values[2], 55, 66, 77, 19, 47, 0x1234567, 88));
  return 0;
}

static int check_static_local_objects(void) {
  static struct overlapping_bitfields value = {
      .prefix = {91, 92, 93},
      .head = 21,
      .middle = 49,
      .wide = 0x3456789,
      .tail = 94,
  };
  static struct overlapping_bitfields values[] = {
      [1] = {
          .prefix = {95, 96, 97},
          .head = 23,
          .middle = 51,
          .wide = 0x2345678,
          .tail = 98,
      },
  };

  assert(value_matches(
      &value, 91, 92, 93, 21, 49, 0x3456789, 94));
  assert(sizeof(values) / sizeof(values[0]) == 2);
  assert(value_matches(
      &values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[1], 95, 96, 97, 23, 51, 0x2345678, 98));
  return 0;
}

static int check_automatic_objects(void) {
  struct overlapping_bitfields value = {
      .prefix = {101, 102, 103},
      .head = 25,
      .middle = 53,
      .wide = 0x16789ab,
      .tail = 104,
  };
  struct overlapping_bitfields values[3] = {
      [2] = {
          .prefix = {105, 106, 107},
          .head = 27,
          .middle = 55,
          .wide = 0x26789ab,
          .tail = 108,
      },
  };

  assert(value_matches(
      &value, 101, 102, 103, 25, 53, 0x16789ab, 104));
  assert(value_matches(
      &values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[1], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[2], 105, 106, 107, 27, 55, 0x26789ab, 108));
  return 0;
}

static int check_compound_literals(void) {
  struct overlapping_bitfields *value =
      &(struct overlapping_bitfields){
          .prefix = {109, 110, 111},
          .head = 29,
          .middle = 57,
          .wide = 0x36789ab,
          .tail = 112,
      };
  struct overlapping_bitfields *values =
      (struct overlapping_bitfields[]){
          [1] = {
              .prefix = {113, 114, 115},
              .head = 31,
              .middle = 59,
              .wide = 0x3abcdef,
              .tail = 116,
          },
      };

  assert(value_matches(
      value, 109, 110, 111, 29, 57, 0x36789ab, 112));
  assert(value_matches(
      &values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[1], 113, 114, 115, 31, 59, 0x3abcdef, 116));
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
