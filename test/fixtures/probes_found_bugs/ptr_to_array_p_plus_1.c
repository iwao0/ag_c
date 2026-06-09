// int (*p)[3] = arr; (*(p+1))[0]
int main(void) {
  int arr[2][3] = { {1,2,3}, {4,5,6} };
  int (*p)[3] = arr;
  return (*(p+1))[0];  // arr[1][0] = 4
}
// 期待: 4
