// 関数戻り値が配列ポインタ (typedef array)
int g_arr[5] = {10, 20, 30, 40, 50};
int *get_arr(void) { return g_arr; }
int main(void) {
  return get_arr()[2] + get_arr()[4];  // 30 + 50 = 80
}
// 期待: 80
