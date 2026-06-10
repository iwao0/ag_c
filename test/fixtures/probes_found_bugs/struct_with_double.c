// double メンバを持つ struct
struct V { double x; double y; };
int main(void) {
  struct V v = {1.5, 2.5};
  return (int)(v.x + v.y);  // 4
}
// 期待: 4
