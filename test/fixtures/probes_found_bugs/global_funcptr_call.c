// グローバル関数ポインタ
int add(int a, int b) { return a + b; }
int (*gp)(int, int) = add;
int main(void) {
  return gp(20, 22);
}
// 期待: 42
