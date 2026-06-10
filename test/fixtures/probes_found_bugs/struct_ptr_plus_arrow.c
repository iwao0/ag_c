// struct ポインタ算術
struct P { int x; int y; };
int main(void) {
  struct P arr[3] = { {1,2}, {3,4}, {5,6} };
  struct P *p = arr;
  return (p+1)->x + (p+2)->y;  // 3 + 6 = 9
}
// 期待: 9
