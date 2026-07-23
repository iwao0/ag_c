#include <assert.h>

struct deep_numbers {
  union {
    struct {
      union {
        int inner_first;
        struct {
          int first;
          int second;
        } pair;
      };
    };
    long long outer_alternate;
  };
  int tail;
};

static int global_value = 21;
static int second_global_value = 23;

static int plus_one(int value) {
  return value + 1;
}

static int plus_two(int value) {
  return value + 2;
}

struct deep_references {
  union {
    struct {
      union {
        long long raw;
        struct {
          int *data;
          int (*callback)(int);
        } references;
      };
    };
    long long outer_raw[2];
  };
  int tail;
};

static struct deep_numbers global_numbers = {
    .outer_alternate = -1,
    .pair = {11, 12},
    .tail = 13,
};

static struct deep_references global_references = {
    .outer_raw = {-1, -1},
    .references = {
        .data = &global_value,
        .callback = plus_one,
    },
    .tail = 22,
};

static struct deep_references global_reference_array[] = {
    [1] = {
        .outer_raw[1] = -1,
        .references = {
            .data = &global_value,
            .callback = plus_one,
        },
        .tail = 24,
    },
    [2] = {
        .references = {
            .data = &second_global_value,
            .callback = plus_two,
        },
        .tail = 25,
    },
};

static int references_match(
    const struct deep_references *value, int *data,
    int expected_result, int expected_tail) {
  return value->references.data == data &&
         *value->references.data == *data &&
         value->references.callback != 0 &&
         value->references.callback(30) == expected_result &&
         value->tail == expected_tail;
}

static int check_global_objects(void) {
  assert(global_numbers.pair.first == 11);
  assert(global_numbers.pair.second == 12);
  assert(global_numbers.tail == 13);
  assert(references_match(
      &global_references, &global_value, 31, 22));

  assert(sizeof(global_reference_array) /
             sizeof(global_reference_array[0]) ==
         3);
  assert(global_reference_array[0].references.data == 0);
  assert(global_reference_array[0].references.callback == 0);
  assert(global_reference_array[0].tail == 0);
  assert(references_match(
      &global_reference_array[1], &global_value, 31, 24));
  assert(references_match(
      &global_reference_array[2], &second_global_value, 32, 25));
  return 0;
}

static int check_static_local_objects(void) {
  static struct deep_numbers numbers = {
      .outer_alternate = -1,
      .pair.second = 32,
      .pair.first = 31,
      .tail = 33,
  };
  static struct deep_references references[] = {
      [1] = {
          .outer_raw[0] = -1,
          .references.data = &second_global_value,
          .references.callback = plus_two,
          .tail = 34,
      },
  };

  assert(numbers.pair.first == 31);
  assert(numbers.pair.second == 32);
  assert(numbers.tail == 33);
  assert(references[0].references.data == 0);
  assert(references[0].references.callback == 0);
  assert(references_match(
      &references[1], &second_global_value, 32, 34));
  return 0;
}

static int check_automatic_objects(void) {
  int local_value = 41;
  struct deep_numbers numbers = {
      .outer_alternate = -1,
      .pair = {42, 43},
      .tail = 44,
  };
  struct deep_references references = {
      .outer_raw = {-1, -1},
      .references = {
          .data = &local_value,
          .callback = plus_one,
      },
      .tail = 45,
  };

  assert(numbers.pair.first == 42);
  assert(numbers.pair.second == 43);
  assert(numbers.tail == 44);
  assert(references_match(
      &references, &local_value, 31, 45));
  return 0;
}

static int check_compound_literals(void) {
  struct deep_numbers *numbers =
      &(struct deep_numbers){
          .outer_alternate = -1,
          .pair = {51, 52},
          .tail = 53,
      };
  struct deep_references *references =
      &(struct deep_references){
          .outer_raw[1] = -1,
          .references = {
              .data = &global_value,
              .callback = plus_two,
          },
          .tail = 54,
      };

  assert(numbers->pair.first == 51);
  assert(numbers->pair.second == 52);
  assert(numbers->tail == 53);
  assert(references_match(
      references, &global_value, 32, 54));
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
