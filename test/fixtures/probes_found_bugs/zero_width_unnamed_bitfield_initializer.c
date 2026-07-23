#include <assert.h>

/*
 * The first named bit-field shares the allocation unit containing prefix.
 * A zero-width field then starts a fresh unit, while an unnamed nonzero field
 * reserves bits inside that second unit. Initializers must skip both unnamed
 * fields without losing the allocation-unit boundary or adjacent bytes.
 */
struct split_bitfields {
  unsigned char prefix[3];
  unsigned int head : 5;
  unsigned int : 0;
  unsigned int middle : 6;
  unsigned int : 3;
  signed int signed_value : 7;
  unsigned char tail;
};

static struct split_bitfields global_value = {
    .prefix = {11, 22, 33},
    .head = 17,
    .middle = 45,
    .signed_value = -23,
    .tail = 44,
};

static struct split_bitfields global_values[] = {
    [2] = {
        .prefix = {55, 66, 77},
        .head = 19,
        .middle = 47,
        .signed_value = -25,
        .tail = 88,
    },
};

static int value_matches(
    const struct split_bitfields *value,
    unsigned char first, unsigned char second, unsigned char third,
    unsigned int head, unsigned int middle, int signed_value,
    unsigned char tail) {
  return value->prefix[0] == first &&
         value->prefix[1] == second &&
         value->prefix[2] == third &&
         value->head == head &&
         value->middle == middle &&
         value->signed_value == signed_value &&
         value->tail == tail;
}

static int check_global_objects(void) {
  assert(sizeof(struct split_bitfields) == 8);
  assert(value_matches(
      &global_value, 11, 22, 33, 17, 45, -23, 44));

  assert(sizeof(global_values) / sizeof(global_values[0]) == 3);
  assert(value_matches(
      &global_values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &global_values[1], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &global_values[2], 55, 66, 77, 19, 47, -25, 88));
  return 0;
}

static int check_static_local_objects(void) {
  static struct split_bitfields value = {
      .prefix = {91, 92, 93},
      .head = 21,
      .middle = 49,
      .signed_value = -27,
      .tail = 94,
  };
  static struct split_bitfields values[] = {
      [1] = {
          .prefix = {95, 96, 97},
          .head = 23,
          .middle = 51,
          .signed_value = -29,
          .tail = 98,
      },
  };

  assert(value_matches(
      &value, 91, 92, 93, 21, 49, -27, 94));
  assert(sizeof(values) / sizeof(values[0]) == 2);
  assert(value_matches(
      &values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[1], 95, 96, 97, 23, 51, -29, 98));
  return 0;
}

static int check_automatic_objects(void) {
  struct split_bitfields value = {
      .prefix = {101, 102, 103},
      .head = 25,
      .middle = 53,
      .signed_value = -31,
      .tail = 104,
  };
  struct split_bitfields values[3] = {
      [2] = {
          .prefix = {105, 106, 107},
          .head = 27,
          .middle = 55,
          .signed_value = -17,
          .tail = 108,
      },
  };

  assert(value_matches(
      &value, 101, 102, 103, 25, 53, -31, 104));
  assert(value_matches(
      &values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[1], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[2], 105, 106, 107, 27, 55, -17, 108));
  return 0;
}

static int check_compound_literals(void) {
  struct split_bitfields *value =
      &(struct split_bitfields){
          .prefix = {109, 110, 111},
          .head = 29,
          .middle = 57,
          .signed_value = -19,
          .tail = 112,
      };
  struct split_bitfields *values =
      (struct split_bitfields[]){
          [1] = {
              .prefix = {113, 114, 115},
              .head = 31,
              .middle = 59,
              .signed_value = -21,
              .tail = 116,
          },
      };

  assert(value_matches(
      value, 109, 110, 111, 29, 57, -19, 112));
  assert(value_matches(
      &values[0], 0, 0, 0, 0, 0, 0, 0));
  assert(value_matches(
      &values[1], 113, 114, 115, 31, 59, -21, 116));
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
