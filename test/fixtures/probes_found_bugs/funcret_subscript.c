// 関数戻り値が配列ポインタ (typedef array)
#include <assert.h>
int g_arr[5] = {10, 20, 30, 40, 50};
int *get_arr(void) { return g_arr; }
int main(void) {
  assert(get_arr()[2] == 30); assert(get_arr()[4] == 50); return 0;  // 30 + 50 = 80
}
// 期待: 80
