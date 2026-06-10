// ネストした designated initializer `.a.y = 5`
// 修正前: parse_struct_initializer の `.` 分岐が `.member = val` か
// `.member[idx] = val` の 2 形のみで、`.member1.member2 = val` を
// 受け付けていなかった (E2006: '=' を期待しているのに '.' が来た)。
// 加えて、struct 全体の zero-fill が抜けていたため、明示指定されなかった
// struct メンバ (.a.x) がスタックの未初期化値のままになっていた。
struct P { int x; int y; };
struct B { struct P a; struct P b; };
int main(void) {
  struct B b = { .a.y = 5, .b.x = 7, .b.y = 9 };
  return b.a.x + b.a.y + b.b.x + b.b.y; // 0+5+7+9 = 21
}
// 期待: 21
