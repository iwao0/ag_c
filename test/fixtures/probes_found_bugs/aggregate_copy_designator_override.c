#include <assert.h>

struct copy_pair {
  int left;
  int right;
};

struct copy_holder {
  unsigned char prefix;
  struct copy_pair pair;
  struct copy_pair pairs[2];
  unsigned char suffix;
};

static struct copy_pair make_pair(int left, int right) {
  struct copy_pair value = {left, right};
  return value;
}

int main(void) {
  struct copy_pair first = {10, 20};
  struct copy_pair second = {30, 40};
  struct copy_holder partial_after_copy = {
      .prefix = 1,
      .pair = first,
      .pair.right = 21,
      .pairs[1] = second,
      .pairs[1].left = 31,
      .suffix = 2,
  };
  struct copy_holder copy_after_partial = {
      .prefix = 3,
      .pair.left = 50,
      .pair = make_pair(51, 52),
      .pairs[1].right = 60,
      .pairs[1] = make_pair(61, 62),
      .suffix = 4,
  };
  struct copy_holder all_fields_after_copy = {
      .prefix = 5,
      .pair = first,
      .pair.left = 70,
      .pair.right = 71,
      .suffix = 6,
  };

  assert(partial_after_copy.prefix == 1);
  assert(partial_after_copy.pair.left == 10);
  assert(partial_after_copy.pair.right == 21);
  assert(partial_after_copy.pairs[0].left == 0);
  assert(partial_after_copy.pairs[0].right == 0);
  assert(partial_after_copy.pairs[1].left == 31);
  assert(partial_after_copy.pairs[1].right == 40);
  assert(partial_after_copy.suffix == 2);

  assert(copy_after_partial.prefix == 3);
  assert(copy_after_partial.pair.left == 51);
  assert(copy_after_partial.pair.right == 52);
  assert(copy_after_partial.pairs[0].left == 0);
  assert(copy_after_partial.pairs[0].right == 0);
  assert(copy_after_partial.pairs[1].left == 61);
  assert(copy_after_partial.pairs[1].right == 62);
  assert(copy_after_partial.suffix == 4);

  assert(all_fields_after_copy.prefix == 5);
  assert(all_fields_after_copy.pair.left == 70);
  assert(all_fields_after_copy.pair.right == 71);
  assert(all_fields_after_copy.suffix == 6);
  return 0;
}
