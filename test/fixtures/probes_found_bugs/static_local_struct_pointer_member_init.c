#include <assert.h>

int data_values[5] = {1, 3, 5, 7, 9};

struct PairRef {
  int *first;
  int *second;
};

int sum_and_bump_refs(void) {
  static struct PairRef refs[2] = {
      {&data_values[0], &data_values[2]},
      {&data_values[3], &data_values[4]},
  };
  *refs[0].first += 1;
  *refs[1].second += *refs[0].first;
  return *refs[0].second + *refs[1].first + *refs[1].second;
}

int main(void) {
  assert(sum_and_bump_refs() == 23);
  assert(data_values[0] == 2);
  assert(data_values[4] == 11);
  assert(sum_and_bump_refs() == 26);
  assert(data_values[0] == 3);
  assert(data_values[4] == 14);
  return 0;
}
