// グローバル構造体ポインタ
struct P { int x; };
struct P gp = {42};
struct P *pp = &gp;
int main(void) {
  return pp->x;
}
// 期待: 42
