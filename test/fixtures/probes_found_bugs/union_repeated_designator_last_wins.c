#include <assert.h>

struct repeated_pair {
  unsigned int left;
  unsigned int right;
};

union repeated_value {
  struct repeated_pair pair;
  double number;
  unsigned char raw[16];
};

struct repeated_holder {
  unsigned char prefix;
  union repeated_value values[3];
  unsigned char suffix;
};

union repeated_inner {
  unsigned char raw[8];
  unsigned int word;
};

union repeated_outer {
  union repeated_inner inner;
  double number;
  unsigned char raw[16];
};

static union repeated_value global_values[3] = {
    [0].pair = {1, 2},
    [0].number = 3.5,
    [1].number = 4.5,
    [1].pair.right = 7,
    [2].raw[5] = 9,
    [2].raw[2] = 6,
};

static struct repeated_holder global_holder = {
    .prefix = 10,
    .values = {
        [0].pair = {11, 12},
        [0].number = 5.5,
        [1].number = 6.5,
        [1].pair.right = 13,
        [2].raw[9] = 14,
        [2].raw[4] = 15,
    },
    .suffix = 16,
};

static union repeated_outer nested_preserve = {
    .inner.raw[5] = 27,
    .inner.raw[2] = 28,
};

static union repeated_outer nested_reset = {
    .inner.raw[5] = 29,
    .number = 11.5,
    .inner.raw[2] = 30,
};

static union repeated_value whole_member_reset = {
    .raw[5] = 31,
    .raw = {[2] = 32},
};

static union repeated_outer nested_whole_member_reset = {
    .inner.raw[5] = 33,
    .inner = {.raw = {[2] = 34}},
};

static int values_match(const union repeated_value *values,
                        double number, unsigned int right,
                        unsigned char first_raw_index,
                        unsigned char first_raw_value,
                        unsigned char second_raw_index,
                        unsigned char second_raw_value) {
  return values[0].number == number &&
         values[1].pair.left == 0 &&
         values[1].pair.right == right &&
         values[2].raw[first_raw_index] == first_raw_value &&
         values[2].raw[second_raw_index] == second_raw_value;
}

static const union repeated_value *static_values(void) {
  static union repeated_value values[3] = {
      [0].pair = {17, 18},
      [0].number = 7.5,
      [1].number = 8.5,
      [1].pair.right = 19,
      [2].raw[13] = 20,
      [2].raw[6] = 21,
  };
  return values;
}

int main(void) {
  union repeated_value local_values[3] = {
      [0].pair = {22, 23},
      [0].number = 9.5,
      [1].number = 10.5,
      [1].pair.right = 24,
      [2].raw[15] = 25,
      [2].raw[8] = 26,
  };
  union repeated_value local_whole_member_reset = {
      .raw[7] = 35,
      .raw = {[3] = 36},
  };

  assert(values_match(global_values, 3.5, 7, 5, 9, 2, 6));
  assert(values_match(static_values(), 7.5, 19, 13, 20, 6, 21));
  assert(values_match(local_values, 9.5, 24, 15, 25, 8, 26));
  assert(global_holder.prefix == 10);
  assert(values_match(global_holder.values, 5.5, 13, 9, 14, 4, 15));
  assert(global_holder.suffix == 16);
  assert(nested_preserve.inner.raw[5] == 27);
  assert(nested_preserve.inner.raw[2] == 28);
  assert(nested_reset.inner.raw[5] == 0);
  assert(nested_reset.inner.raw[2] == 30);
  assert(whole_member_reset.raw[5] == 0);
  assert(whole_member_reset.raw[2] == 32);
  assert(nested_whole_member_reset.inner.raw[5] == 0);
  assert(nested_whole_member_reset.inner.raw[2] == 34);
  assert(local_whole_member_reset.raw[7] == 0);
  assert(local_whole_member_reset.raw[3] == 36);
  return 0;
}
