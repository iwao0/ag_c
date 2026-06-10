// 構造体配列パラメータ
struct P { int x; int y; };
int sum_x(struct P *arr, int n) {
  int s = 0;
  for (int i = 0; i < n; i++) s += arr[i].x;
  return s;
}
int main(void) {
  struct P pts[3] = { {1,2}, {3,4}, {5,6} };
  return sum_x(pts, 3);
}
// 期待: 1+3+5 = 9
