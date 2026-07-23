#include <assert.h>

/*
 * Every anonymous union starts at the same byte offset. Switching the middle
 * or outer union must discard only descendant activations; later promoted
 * designators must be able to select the nested struct members again.
 */
struct triple_nested_value {
  union {
    struct {
      union {
        struct {
          union {
            long long inner_raw;
            struct {
              int first;
              int second;
            };
          };
          int middle_tail;
        };
        long long middle_raw[2];
      };
    };
    long long outer_raw[3];
  };
  int tail;
};

static struct triple_nested_value global_value = {
    .first = 1,
    .second = 2,
    .middle_tail = 3,
    .middle_raw = {-1, -1},
    .second = 4,
    .first = 5,
    .middle_tail = 6,
    .outer_raw[2] = -1,
    .second = 7,
    .first = 8,
    .middle_tail = 9,
    .tail = 10,
};

static struct triple_nested_value global_values[] = {
    [2].first = 11,
    [2].middle_raw[1] = -1,
    [2].second = 12,
    [2].first = 13,
    [2].middle_tail = 14,
    [2].tail = 15,
};

static int value_matches(
    const struct triple_nested_value *value,
    int first, int second, int middle_tail, int tail) {
  return value->first == first &&
         value->second == second &&
         value->middle_tail == middle_tail &&
         value->tail == tail;
}

static int check_global_objects(void) {
  assert(value_matches(&global_value, 8, 7, 9, 10));

  assert(sizeof(global_values) / sizeof(global_values[0]) == 3);
  assert(global_values[0].outer_raw[0] == 0);
  assert(global_values[0].outer_raw[1] == 0);
  assert(global_values[0].outer_raw[2] == 0);
  assert(global_values[0].tail == 0);
  assert(global_values[1].outer_raw[0] == 0);
  assert(global_values[1].outer_raw[1] == 0);
  assert(global_values[1].outer_raw[2] == 0);
  assert(global_values[1].tail == 0);
  assert(value_matches(&global_values[2], 13, 12, 14, 15));
  return 0;
}

static int check_static_local_objects(void) {
  static struct triple_nested_value value = {
      .first = 16,
      .middle_raw[0] = -1,
      .second = 17,
      .first = 18,
      .middle_tail = 19,
      .outer_raw[1] = -1,
      .first = 20,
      .second = 21,
      .middle_tail = 22,
      .tail = 23,
  };
  static struct triple_nested_value values[] = {
      [1] = {
          .inner_raw = -1,
          .first = 24,
          .second = 25,
          .middle_tail = 26,
          .tail = 27,
      },
  };

  assert(value_matches(&value, 20, 21, 22, 23));
  assert(sizeof(values) / sizeof(values[0]) == 2);
  assert(values[0].outer_raw[0] == 0);
  assert(values[0].outer_raw[1] == 0);
  assert(values[0].outer_raw[2] == 0);
  assert(values[0].tail == 0);
  assert(value_matches(&values[1], 24, 25, 26, 27));
  return 0;
}

static int check_automatic_objects(void) {
  struct triple_nested_value value = {
      .first = 28,
      .second = 29,
      .middle_tail = 30,
      .middle_raw[1] = -1,
      .first = 31,
      .second = 32,
      .middle_tail = 33,
      .outer_raw[0] = -1,
      .second = 34,
      .first = 35,
      .middle_tail = 36,
      .tail = 37,
  };
  struct triple_nested_value values[3] = {
      [2] = {
          .outer_raw[2] = -1,
          .first = 38,
          .second = 39,
          .middle_tail = 40,
          .tail = 41,
      },
  };

  assert(value_matches(&value, 35, 34, 36, 37));
  assert(values[0].outer_raw[0] == 0);
  assert(values[1].outer_raw[0] == 0);
  assert(value_matches(&values[2], 38, 39, 40, 41));
  return 0;
}

static int check_compound_literals(void) {
  struct triple_nested_value *value =
      &(struct triple_nested_value){
          .middle_raw[0] = -1,
          .first = 42,
          .second = 43,
          .middle_tail = 44,
          .outer_raw[2] = -1,
          .first = 45,
          .second = 46,
          .middle_tail = 47,
          .tail = 48,
      };
  struct triple_nested_value *values =
      (struct triple_nested_value[]){
          [1] = {
              .inner_raw = -1,
              .second = 49,
              .first = 50,
              .middle_tail = 51,
              .tail = 52,
          },
      };

  assert(value_matches(value, 45, 46, 47, 48));
  assert(values[0].outer_raw[0] == 0);
  assert(values[0].outer_raw[1] == 0);
  assert(values[0].outer_raw[2] == 0);
  assert(values[0].tail == 0);
  assert(value_matches(&values[1], 50, 49, 51, 52));
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
