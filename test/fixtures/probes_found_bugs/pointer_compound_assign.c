// compound assign on pointer
int main(void) {
  int arr[5] = {1, 2, 3, 4, 5};
  int *p = arr;
  p += 2;  // points to arr[2]
  return *p;
}
// 期待: 3
