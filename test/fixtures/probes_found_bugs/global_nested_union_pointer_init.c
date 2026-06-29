#include <assert.h>

int nums[4] = {2, 4, 6, 8};

union Slot {
  int *p;
  long raw;
};

struct Box {
  int tag;
  union Slot slots[2];
};

struct Box boxes[2] = {
    {1, {{.p = &nums[0]}, {.p = &nums[1]}}},
    {2, {{.raw = 0}, {.p = &nums[3]}}},
};

int main(void) {
  assert(boxes[0].tag == 1);
  assert(*boxes[0].slots[0].p == 2);
  assert(*boxes[0].slots[1].p == 4);
  assert(boxes[1].slots[0].raw == 0);
  assert(*boxes[1].slots[1].p == 8);
  *boxes[1].slots[1].p += boxes[0].tag + boxes[1].tag;
  assert(nums[3] == 11);
  return 0;
}
