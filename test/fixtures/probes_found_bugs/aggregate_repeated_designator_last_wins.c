#include <assert.h>

struct repeated_aggregate_pair {
  int left;
  int right;
};

struct repeated_aggregate_state {
  unsigned char prefix;
  int values[4];
  struct repeated_aggregate_pair pair;
  unsigned char suffix;
};

static struct repeated_aggregate_state global_whole_after_partial = {
    .prefix = 1,
    .values[1] = 11,
    .values = {[2] = 22},
    .pair.left = 33,
    .pair = {.right = 44},
    .suffix = 2,
};

static struct repeated_aggregate_state global_partial_after_whole = {
    .prefix = 3,
    .values = {4, 5, 6, 7},
    .values[1] = 8,
    .pair = {9, 10},
    .pair.right = 11,
    .suffix = 12,
};

static int whole_after_partial_matches(
    const struct repeated_aggregate_state *state,
    unsigned char prefix, int array_value, int pair_value,
    unsigned char suffix) {
  return state->prefix == prefix &&
         state->values[0] == 0 &&
         state->values[1] == 0 &&
         state->values[2] == array_value &&
         state->values[3] == 0 &&
         state->pair.left == 0 &&
         state->pair.right == pair_value &&
         state->suffix == suffix;
}

static int partial_after_whole_matches(
    const struct repeated_aggregate_state *state,
    unsigned char prefix, int replacement, int left,
    int right, unsigned char suffix) {
  return state->prefix == prefix &&
         state->values[0] == 4 &&
         state->values[1] == replacement &&
         state->values[2] == 6 &&
         state->values[3] == 7 &&
         state->pair.left == left &&
         state->pair.right == right &&
         state->suffix == suffix;
}

static const struct repeated_aggregate_state *static_whole_after_partial(void) {
  static struct repeated_aggregate_state state = {
      .prefix = 13,
      .values[1] = 14,
      .values = {[2] = 15},
      .pair.left = 16,
      .pair = {.right = 17},
      .suffix = 18,
  };
  return &state;
}

int main(void) {
  struct repeated_aggregate_state local_whole_after_partial = {
      .prefix = 19,
      .values[1] = 20,
      .values = {[2] = 21},
      .pair.left = 22,
      .pair = {.right = 23},
      .suffix = 24,
  };

  assert(whole_after_partial_matches(
      &global_whole_after_partial, 1, 22, 44, 2));
  assert(partial_after_whole_matches(
      &global_partial_after_whole, 3, 8, 9, 11, 12));
  assert(whole_after_partial_matches(
      static_whole_after_partial(), 13, 15, 17, 18));
  assert(whole_after_partial_matches(
      &local_whole_after_partial, 19, 21, 23, 24));
  return 0;
}
