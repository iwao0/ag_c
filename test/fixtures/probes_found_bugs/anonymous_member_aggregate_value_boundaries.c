// Aggregate value operations must preserve the layout and promoted fields of
// anonymous structure and union members.
// Expected: exit=0
#include <assert.h>

struct small_view {
  union {
    struct {
      unsigned char low;
      unsigned char high;
    };
    unsigned short combined;
  };
  unsigned char tag;
};

struct large_view {
  long head;
  union {
    struct {
      int first;
      int second;
      int third;
    };
    long alternate[2];
  };
  long tail;
};

union small_choice {
  struct {
    int left;
    int right;
  };
  unsigned long long bits;
};

union large_choice {
  struct {
    long head;
    int body[3];
    long tail;
  };
  unsigned char bytes[sizeof(struct large_view)];
};

struct reference_box {
  int marker;
  struct {
    int *object;
    int (*callback)(int);
    const char *text;
  };
  long tail;
};

static int referenced_object = 131;

static int add_five(int value) {
  return value + 5;
}

static const struct small_view global_small = {
    .low = 3, .high = 5, .tag = 7};
static const struct large_view global_large = {
    .head = 19, .first = 23, .second = 29,
    .third = 31, .tail = 37};
static const union small_choice global_small_choice = {
    .left = 61, .right = 67};
static const union large_choice global_large_choice = {
    .head = 79, .body = {83, 89, 97}, .tail = 101};
static const struct reference_box global_reference = {
    .marker = 137,
    .object = &referenced_object,
    .callback = add_five,
    .text = "anonymous",
    .tail = 139};

static int small_sum(struct small_view value) {
  return value.low + value.high + value.tag;
}

static long large_sum(struct large_view value) {
  return value.head + value.first + value.second +
         value.third + value.tail;
}

static int small_choice_sum(union small_choice value) {
  return value.left + value.right;
}

static long large_choice_sum(union large_choice value) {
  return value.head + value.body[0] + value.body[1] +
         value.body[2] + value.tail;
}

static long reference_sum(struct reference_box value) {
  return value.marker + value.callback(*value.object) +
         value.text[2] + value.tail;
}

static struct small_view rotate_small(
    struct small_view value) {
  return (struct small_view){
      .low = value.high,
      .high = value.tag,
      .tag = value.low};
}

static struct large_view rotate_large(
    struct large_view value) {
  return (struct large_view){
      .head = value.tail,
      .first = value.third,
      .second = value.first,
      .third = value.second,
      .tail = value.head};
}

static union small_choice swap_small_choice(
    union small_choice value) {
  return (union small_choice){
      .left = value.right, .right = value.left};
}

static union large_choice rotate_large_choice(
    union large_choice value) {
  return (union large_choice){
      .head = value.tail,
      .body = {
          value.body[2], value.body[0], value.body[1]},
      .tail = value.head};
}

static struct reference_box copy_reference(
    struct reference_box value) {
  return value;
}

typedef struct large_view large_transform_t(
    struct large_view);
typedef union large_choice large_choice_transform_t(
    union large_choice);
typedef struct reference_box reference_transform_t(
    struct reference_box);

static void verify_initialization_assignment_and_return(void) {
  struct small_view small_initialized = global_small;
  struct small_view small_target = {
      .low = 0, .high = 0, .tag = 0};
  struct small_view small_assignment =
      (small_target = global_small);
  assert(small_sum(small_initialized) == 15);
  assert(small_sum(small_target) == 15);
  assert(small_sum(small_assignment) == 15);
  assert(small_sum(rotate_small(global_small)) == 15);

  struct large_view large_initialized = global_large;
  struct large_view large_target = {
      .head = 0, .first = 0, .second = 0,
      .third = 0, .tail = 0};
  struct large_view large_assignment =
      (large_target = global_large);
  assert(large_sum(large_initialized) == 139);
  assert(large_sum(large_target) == 139);
  assert(large_sum(large_assignment) == 139);
  assert(large_sum(rotate_large(global_large)) == 139);

  union small_choice small_choice_initialized =
      global_small_choice;
  union small_choice small_choice_target = {
      .left = 0, .right = 0};
  union small_choice small_choice_assignment =
      (small_choice_target = global_small_choice);
  assert(small_choice_sum(small_choice_initialized) == 128);
  assert(small_choice_sum(small_choice_target) == 128);
  assert(small_choice_sum(small_choice_assignment) == 128);
  assert(small_choice_sum(
             swap_small_choice(global_small_choice)) == 128);

  union large_choice large_choice_initialized =
      global_large_choice;
  union large_choice large_choice_target = {
      .head = 0, .body = {0, 0, 0}, .tail = 0};
  union large_choice large_choice_assignment =
      (large_choice_target = global_large_choice);
  assert(large_choice_sum(large_choice_initialized) == 449);
  assert(large_choice_sum(large_choice_target) == 449);
  assert(large_choice_sum(large_choice_assignment) == 449);
  assert(large_choice_sum(
             rotate_large_choice(global_large_choice)) == 449);

  struct reference_box reference_initialized =
      global_reference;
  struct reference_box reference_target = {
      .marker = 0, .object = 0, .callback = 0,
      .text = 0, .tail = 0};
  struct reference_box reference_assignment =
      (reference_target = global_reference);
  assert(reference_sum(reference_initialized) == 523);
  assert(reference_sum(reference_target) == 523);
  assert(reference_sum(reference_assignment) == 523);
  assert(reference_sum(copy_reference(global_reference)) == 523);
}

static void verify_indirect_calls_conditionals_and_comma(void) {
  large_transform_t *large_transform = rotate_large;
  large_choice_transform_t *choice_transform =
      rotate_large_choice;
  reference_transform_t *reference_transform =
      copy_reference;
  struct large_view indirect_large =
      large_transform(global_large);
  union large_choice indirect_choice =
      choice_transform(global_large_choice);
  struct reference_box indirect_reference =
      reference_transform(global_reference);
  assert(large_sum(indirect_large) == 139);
  assert(large_choice_sum(indirect_choice) == 449);
  assert(reference_sum(indirect_reference) == 523);

  struct small_view small_right = {
      .low = 11, .high = 13, .tag = 17};
  struct large_view large_right = {
      .head = 41, .first = 43, .second = 47,
      .third = 53, .tail = 59};
  union small_choice small_choice_right = {
      .left = 71, .right = 73};
  union large_choice large_choice_right = {
      .head = 103, .body = {107, 109, 113}, .tail = 127};
  int choose_left = 1;
  int comma_evaluations = 0;

  struct small_view selected_small =
      choose_left ? global_small : small_right;
  struct large_view selected_large =
      choose_left ? global_large : large_right;
  union small_choice selected_small_choice =
      choose_left ? global_small_choice : small_choice_right;
  union large_choice selected_large_choice =
      choose_left ? global_large_choice : large_choice_right;
  assert(small_sum(selected_small) == 15);
  assert(large_sum(selected_large) == 139);
  assert(small_choice_sum(selected_small_choice) == 128);
  assert(large_choice_sum(selected_large_choice) == 449);

  choose_left = 0;
  selected_small =
      choose_left ? global_small : small_right;
  selected_large =
      choose_left ? global_large : large_right;
  selected_small_choice =
      choose_left ? global_small_choice : small_choice_right;
  selected_large_choice =
      choose_left ? global_large_choice : large_choice_right;
  assert(small_sum(selected_small) == 41);
  assert(large_sum(selected_large) == 243);
  assert(small_choice_sum(selected_small_choice) == 144);
  assert(large_choice_sum(selected_large_choice) == 559);

  struct small_view comma_small =
      (comma_evaluations++, global_small);
  struct large_view comma_large =
      (comma_evaluations++, large_right);
  union small_choice comma_small_choice =
      (comma_evaluations++, global_small_choice);
  union large_choice comma_large_choice =
      (comma_evaluations++, large_choice_right);
  struct reference_box comma_reference =
      (comma_evaluations++, global_reference);
  assert(small_sum(comma_small) == 15);
  assert(large_sum(comma_large) == 243);
  assert(small_choice_sum(comma_small_choice) == 128);
  assert(large_choice_sum(comma_large_choice) == 559);
  assert(reference_sum(comma_reference) == 523);
  assert(comma_evaluations == 5);
}

int main(void) {
  verify_initialization_assignment_and_return();
  verify_indirect_calls_conditionals_and_comma();
  return 0;
}
