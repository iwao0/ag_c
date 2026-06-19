// compound assign on pointer
#include <assert.h>
int main(void) {
  int arr[5] = {1, 2, 3, 4, 5};
  int *p = arr;
  p += 2;  // points to arr[2]
  assert(*p == 3); return 0;
}
// 期待: 3
