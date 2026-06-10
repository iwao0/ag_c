// (cond ? a : b).x
struct V { int v; };
int main(void) {
  struct V a = {10};
  struct V b = {20};
  int cond = 1;
  return (cond ? a : b).v;
}
// 期待: 10
