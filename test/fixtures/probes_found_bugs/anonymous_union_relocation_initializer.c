#include <assert.h>

static int global_number = 41;
static int second_global_number = 43;

static int plus_one(int value) {
  return value + 1;
}

static int plus_two(int value) {
  return value + 2;
}

union anonymous_references {
  struct {
    int *data;
    int (*callback)(int);
  };
  long long raw[2];
};

struct anonymous_reference_holder {
  union {
    struct {
      int *data;
      int (*callback)(int);
    };
    long long raw[2];
  };
  int tail;
};

static union anonymous_references global_references = {
    .raw = {-1, -1},
    .data = &global_number,
    .callback = plus_one,
};

static struct anonymous_reference_holder global_holder = {
    .raw = {-1, -1},
    .data = &global_number,
    plus_one,
    44,
};

static union anonymous_references global_reference_array[] = {
    [1].raw[1] = -1,
    [1].data = &global_number,
    plus_one,
    &second_global_number,
    plus_two,
};

static struct anonymous_reference_holder global_holder_array[] = {
    [2] = {
        .raw[1] = -1,
        .data = &second_global_number,
        .callback = plus_two,
        .tail = 49,
    },
};

static int references_match(const union anonymous_references *value,
                            int *data, int expected_result) {
  return value->data == data && *value->data == *data &&
         value->callback != 0 &&
         value->callback(10) == expected_result;
}

static int check_global_objects(void) {
  assert(references_match(
      &global_references, &global_number, 11));
  assert(global_holder.data == &global_number);
  assert(*global_holder.data == 41);
  assert(global_holder.callback != 0);
  assert(global_holder.callback(20) == 21);
  assert(global_holder.tail == 44);

  assert(sizeof(global_reference_array) /
             sizeof(global_reference_array[0]) ==
         3);
  assert(global_reference_array[0].data == 0);
  assert(references_match(
      &global_reference_array[1], &global_number, 11));
  assert(references_match(
      &global_reference_array[2], &second_global_number, 12));

  assert(sizeof(global_holder_array) /
             sizeof(global_holder_array[0]) ==
         3);
  assert(global_holder_array[0].data == 0);
  assert(global_holder_array[0].callback == 0);
  assert(global_holder_array[0].tail == 0);
  assert(global_holder_array[1].data == 0);
  assert(global_holder_array[1].callback == 0);
  assert(global_holder_array[1].tail == 0);
  assert(global_holder_array[2].data == &second_global_number);
  assert(global_holder_array[2].callback != 0);
  assert(global_holder_array[2].callback(20) == 22);
  assert(global_holder_array[2].tail == 49);
  return 0;
}

static int check_static_local_objects(void) {
  static union anonymous_references references = {
      .raw = {-1, -1},
      .data = &second_global_number,
      .callback = plus_two,
  };
  static struct anonymous_reference_holder holder = {
      .raw[1] = -1,
      .data = &second_global_number,
      plus_two,
      45,
  };
  static union anonymous_references reference_array[] = {
      [1] = {
          .raw[0] = -1,
          .data = &global_number,
          .callback = plus_one,
      },
  };

  assert(references_match(
      &references, &second_global_number, 12));
  assert(holder.data == &second_global_number);
  assert(holder.callback != 0);
  assert(holder.callback(30) == 32);
  assert(holder.tail == 45);
  assert(sizeof(reference_array) / sizeof(reference_array[0]) == 2);
  assert(reference_array[0].data == 0);
  assert(reference_array[0].callback == 0);
  assert(references_match(
      &reference_array[1], &global_number, 11));
  return 0;
}

static int check_automatic_objects(void) {
  int local_number = 46;
  union anonymous_references references = {
      .raw = {-1, -1},
      .data = &local_number,
      .callback = plus_one,
  };
  struct anonymous_reference_holder holder = {
      .raw[0] = -1,
      .data = &local_number,
      plus_two,
      47,
  };
  union anonymous_references reference_array[3] = {
      [2] = {
          .raw[1] = -1,
          .data = &local_number,
          .callback = plus_one,
      },
  };

  assert(references_match(&references, &local_number, 11));
  assert(holder.data == &local_number);
  assert(holder.callback != 0);
  assert(holder.callback(40) == 42);
  assert(holder.tail == 47);
  assert(reference_array[0].data == 0);
  assert(reference_array[0].callback == 0);
  assert(reference_array[1].data == 0);
  assert(reference_array[1].callback == 0);
  assert(references_match(
      &reference_array[2], &local_number, 11));
  return 0;
}

static int check_compound_literals(void) {
  union anonymous_references *references =
      &(union anonymous_references){
          .raw = {-1, -1},
          .data = &global_number,
          .callback = plus_two,
      };
  struct anonymous_reference_holder *holder =
      &(struct anonymous_reference_holder){
          .raw[1] = -1,
          .data = &second_global_number,
          plus_one,
          48,
      };
  union anonymous_references *reference_array =
      (union anonymous_references[]){
          [1] = {
              .raw[0] = -1,
              .data = &second_global_number,
              .callback = plus_two,
          },
      };

  assert(references_match(
      references, &global_number, 12));
  assert(holder->data == &second_global_number);
  assert(holder->callback != 0);
  assert(holder->callback(50) == 51);
  assert(holder->tail == 48);
  assert(reference_array[0].data == 0);
  assert(reference_array[0].callback == 0);
  assert(references_match(
      &reference_array[1], &second_global_number, 12));
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
