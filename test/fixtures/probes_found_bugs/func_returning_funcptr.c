// 関数戻り値型として関数ポインタ
int sq(int x) { return x * x; }
int cube(int x) { return x * x * x; }
int (*choose(int n))(int) { return n == 2 ? sq : cube; }
int main(void) {
  int (*op)(int) = choose(2);
  return op(5);  // 25
}
// 期待: 25
