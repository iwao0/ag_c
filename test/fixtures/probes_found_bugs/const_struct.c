// const struct メンバアクセス
struct C { int v; };
int main(void) {
  const struct C c = {42};
  return c.v;
}
// 期待: 42
