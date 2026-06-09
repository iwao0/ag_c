// sizeof(int + int) は 4
int main(void) {
  int a = 1, b = 2;
  return (int)sizeof(a + b);
}
// 期待: 4
