// グローバル構造体配列
struct P { int x; int y; };
struct P gpts[3] = { {1,2}, {3,4}, {5,6} };
int main(void) {
  int s = 0;
  for (int i = 0; i < 3; i++) s += gpts[i].x + gpts[i].y;
  return s;
}
// 期待: 21
