// メンバへのポインタ
struct S { int v[3]; };
int main(void) {
  struct S s = { {10, 20, 30} };
  int *p = s.v;
  return p[0] + p[1] + p[2];
}
// 期待: 60
