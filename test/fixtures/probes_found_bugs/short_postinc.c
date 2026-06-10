// short ptr post-inc
int main(void) {
  short arr[5] = {10, 20, 30, 40, 50};
  short *p = arr;
  int sum = 0;
  for (int i = 0; i < 3; i++) sum += *p++;
  return sum;  // 60
}
// 期待: 60
