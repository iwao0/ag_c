// int (*p)[3] = arr; (*p)[0]
int main(void) {
  int arr[2][3] = { {1,2,3}, {4,5,6} };
  int (*p)[3] = arr;
  return (*p)[0];  // arr[0][0] = 1
}
// 期待: 1
