// 関数ポインタ配列を含む struct
int op_add(int a, int b) { return a + b; }
int op_sub(int a, int b) { return a - b; }
struct Ops { int (*fns[2])(int, int); };
int main(void) {
  struct Ops ops;
  ops.fns[0] = op_add;
  ops.fns[1] = op_sub;
  return ops.fns[0](10, 5) + ops.fns[1](20, 3);  // 15 + 17 = 32
}
// 期待: 32
