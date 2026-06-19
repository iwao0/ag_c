// int (*p)[3] = arr; (*p)[0]
#include <assert.h>
int main(void) {
  int arr[2][3] = { {1,2,3}, {4,5,6} };
  int (*p)[3] = arr;
  assert((*p)[0] == 1); return 0;  // arr[0][0] = 1
}
// 期待: 1
