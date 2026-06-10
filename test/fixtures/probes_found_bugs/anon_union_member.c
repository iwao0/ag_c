// 匿名 union (C11)
struct N {
  int kind;
  union { int i; float f; };
};
int main(void) {
  struct N n;
  n.kind = 1; n.i = 42;
  return n.kind * 10 + n.i;
}
// 期待: 52
