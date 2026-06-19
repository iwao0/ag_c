// int (*p)[3] = arr; (*(p+1))[0]
#include <assert.h>
int main(void) {
  int arr[2][3] = { {1,2,3}, {4,5,6} };
  int (*p)[3] = arr;
  assert((*(p+1))[0] == 4); return 0;  // arr[1][0] = 4
}
// 期待: 4
