// typedef of array
#include <assert.h>
typedef int Vec3[3];
int sum(Vec3 v) { return v[0] + v[1] + v[2]; }
int main(void) {
  Vec3 a = {1, 2, 3};
  assert(sum(a) == 6); return 0;
}
// 期待: 6
