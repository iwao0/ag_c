#include <assert.h>

enum arithmetic_value {
  ENUM_BASE = 3,
  ENUM_SHIFTED = (ENUM_BASE + 2) << 3,
  ENUM_NEGATIVE = -ENUM_SHIFTED,
  ENUM_REMAINDER = ENUM_SHIFTED % 7
};

struct pair {
  int left;
  int right;
};

struct nested_pair {
  struct pair nested;
  int tail;
};

static struct pair file_pair = (struct pair){11, 13};

static int sum_pair(struct pair value) {
  return value.left + value.right;
}

static int classify(enum arithmetic_value value) {
  int result = 0;
  switch (value) {
    case ENUM_BASE:
      result += 3;
      /* fall through */
    case ENUM_REMAINDER:
      result += 5;
      break;
    case ENUM_NEGATIVE:
      result = -40;
      break;
    default:
      result = 99;
      break;
  }
  return result;
}

static int check_enum_and_switch(void) {
  assert(ENUM_BASE == 3);
  assert(ENUM_SHIFTED == 40);
  assert(ENUM_NEGATIVE == -40);
  assert(ENUM_REMAINDER == 5);
  assert(classify(ENUM_BASE) == 8);
  assert(classify(ENUM_REMAINDER) == 5);
  assert(classify(ENUM_NEGATIVE) == -40);
  assert(classify((enum arithmetic_value)77) == 99);
  return 0;
}

static int check_loop_goto_and_updates(void) {
  int first;
  int second;
  int third;
  first = second = third = 5;
  assert(first + second + third == 15);

  int index = 0;
  int total = 0;
  do {
    total += index++;
  } while (index < 4);
  assert(total == 6);
  assert(index == 4);

  int before = index++;
  int after = ++index;
  assert(before == 4);
  assert(after == 6);

  int visits = 0;
  goto forward_label;
  visits = -100;
forward_label:
  ++visits;
backward_label:
  ++visits;
  if (visits < 4) {
    goto backward_label;
  }
  assert(visits == 4);
  return 0;
}

static int check_compound_literals(void) {
  assert(file_pair.left == 11);
  assert(file_pair.right == 13);

  struct pair value = (struct pair){17, 19};
  assert(sum_pair(value) == 36);
  assert(sum_pair((struct pair){23, 29}) == 52);

  struct pair *pointer = &(struct pair){31, 37};
  pointer->left += 2;
  assert(pointer->left == 33);
  assert(pointer->right == 37);

  int *array = (int[]){2, 3, 5, 7};
  array[2] += array[0];
  assert(array[0] == 2);
  assert(array[2] == 7);

  struct nested_pair nested =
      (struct nested_pair){{41, 43}, 47};
  assert(nested.nested.left == 41);
  assert(nested.nested.right == 43);
  assert(nested.tail == 47);
  return 0;
}

int main(void) {
  assert(check_enum_and_switch() == 0);
  assert(check_loop_goto_and_updates() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
