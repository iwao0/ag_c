// A structure containing a flexible array member is passed and returned by
// value using only its fixed-size prefix.
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

struct small_flex {
  int length;
  unsigned char data[];
};

struct large_flex {
  long marker;
  int values[3];
  long trailer;
  unsigned char data[];
};

static struct small_flex *small_left;
static struct small_flex *small_right;
static struct large_flex *large_left;
static struct large_flex *large_right;
static int small_left_evaluations;
static int small_right_evaluations;
static int large_left_evaluations;
static int large_right_evaluations;
static int comma_evaluations;

static struct small_flex *select_small_left(void) {
  small_left_evaluations++;
  return small_left;
}

static struct small_flex *select_small_right(void) {
  small_right_evaluations++;
  return small_right;
}

static struct large_flex *select_large_left(void) {
  large_left_evaluations++;
  return large_left;
}

static struct large_flex *select_large_right(void) {
  large_right_evaluations++;
  return large_right;
}

static int consume_small(struct small_flex value) {
  return value.length;
}

static long consume_large(struct large_flex value) {
  return value.marker + value.values[0] +
         value.values[1] + value.values[2] +
         value.trailer;
}

static struct small_flex increment_small(
    struct small_flex value) {
  value.length++;
  return value;
}

static struct large_flex rotate_large(
    struct large_flex value) {
  long marker = value.marker;
  int first = value.values[0];
  value.marker = value.trailer;
  value.values[0] = value.values[2];
  value.values[2] = first;
  value.trailer = marker;
  return value;
}

typedef struct small_flex small_transform_t(
    struct small_flex);
typedef struct large_flex large_transform_t(
    struct large_flex);

static struct small_flex *allocate_small(
    int length, unsigned char seed) {
  struct small_flex *value =
      malloc(sizeof(*value) + 5 * sizeof(value->data[0]));
  assert(value);
  value->length = length;
  for (int i = 0; i < 5; i++)
    value->data[i] = (unsigned char)(seed + i);
  return value;
}

static struct large_flex *allocate_large(
    long marker, int first, int second, int third,
    long trailer, unsigned char seed) {
  struct large_flex *value =
      malloc(sizeof(*value) + 7 * sizeof(value->data[0]));
  assert(value);
  value->marker = marker;
  value->values[0] = first;
  value->values[1] = second;
  value->values[2] = third;
  value->trailer = trailer;
  for (int i = 0; i < 7; i++)
    value->data[i] = (unsigned char)(seed + i);
  return value;
}

static void verify_initialization_calls_and_returns(void) {
  struct small_flex small_snapshot = *small_left;
  assert(small_snapshot.length == 3);
  assert(consume_small(*small_left) == 3);
  assert(consume_small(*small_right) == 7);
  assert(consume_small(increment_small(*small_left)) == 4);

  small_transform_t *small_transform = increment_small;
  struct small_flex transformed_small =
      small_transform(*small_right);
  assert(transformed_small.length == 8);

  struct large_flex large_snapshot = *large_left;
  assert(consume_large(large_snapshot) == 83);
  assert(consume_large(*large_left) == 83);
  assert(consume_large(*large_right) == 181);
  assert(consume_large(rotate_large(*large_left)) == 83);

  large_transform_t *large_transform = rotate_large;
  struct large_flex transformed_large =
      large_transform(*large_right);
  assert(consume_large(transformed_large) == 181);
  assert(transformed_large.marker == 43);
  assert(transformed_large.values[0] == 41);
  assert(transformed_large.values[1] == 37);
  assert(transformed_large.values[2] == 31);
  assert(transformed_large.trailer == 29);
}

static void verify_assignment_result_and_tail_preservation(void) {
  struct small_flex *small_destination =
      allocate_small(0, 0xa0);
  struct small_flex small_assignment =
      (*small_destination = *small_left);
  assert(small_destination->length == 3);
  assert(small_assignment.length == 3);
  for (int i = 0; i < 5; i++)
    assert(small_destination->data[i] ==
           (unsigned char)(0xa0 + i));

  struct large_flex *large_destination =
      allocate_large(0, 0, 0, 0, 0, 0xb0);
  struct large_flex large_assignment =
      (*large_destination = rotate_large(*large_left));
  assert(consume_large(*large_destination) == 83);
  assert(consume_large(large_assignment) == 83);
  assert(large_destination->marker == 23);
  assert(large_destination->values[0] == 19);
  assert(large_destination->values[1] == 17);
  assert(large_destination->values[2] == 13);
  assert(large_destination->trailer == 11);
  for (int i = 0; i < 7; i++)
    assert(large_destination->data[i] ==
           (unsigned char)(0xb0 + i));

  free(small_destination);
  free(large_destination);
}

static void verify_conditionals_and_comma(void) {
  int choose_left = 1;
  struct small_flex selected_small =
      choose_left ? *select_small_left()
                  : *select_small_right();
  struct large_flex selected_large =
      choose_left ? *select_large_left()
                  : *select_large_right();
  assert(selected_small.length == 3);
  assert(consume_large(selected_large) == 83);
  assert(small_left_evaluations == 1);
  assert(small_right_evaluations == 0);
  assert(large_left_evaluations == 1);
  assert(large_right_evaluations == 0);

  choose_left = 0;
  selected_small =
      choose_left ? *select_small_left()
                  : *select_small_right();
  selected_large =
      choose_left ? *select_large_left()
                  : *select_large_right();
  assert(selected_small.length == 7);
  assert(consume_large(selected_large) == 181);
  assert(small_left_evaluations == 1);
  assert(small_right_evaluations == 1);
  assert(large_left_evaluations == 1);
  assert(large_right_evaluations == 1);

  struct small_flex comma_small =
      (comma_evaluations++, *small_left);
  struct large_flex comma_large =
      (comma_evaluations++, *large_right);
  assert(comma_small.length == 3);
  assert(consume_large(comma_large) == 181);
  assert(comma_evaluations == 2);
}

int main(void) {
  small_left = allocate_small(3, 0x10);
  small_right = allocate_small(7, 0x20);
  large_left =
      allocate_large(11, 13, 17, 19, 23, 0x30);
  large_right =
      allocate_large(29, 31, 37, 41, 43, 0x40);

  verify_initialization_calls_and_returns();
  verify_assignment_result_and_tail_preservation();
  verify_conditionals_and_comma();

  free(small_left);
  free(small_right);
  free(large_left);
  free(large_right);
  return 0;
}
