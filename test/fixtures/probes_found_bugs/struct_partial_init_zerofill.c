// struct メンバの部分指定で未指定メンバが 0 で埋まること
// 修正前: parse_struct_initializer の末尾の zero-fill は struct/union/array
// メンバを skip していたため、`{ .a = {1,2} }` のように a だけ指定したとき
// b は未初期化スタック値のままだった (差分テストで garbage exit code 検出)。
struct P { int x; int y; };
struct B { struct P a; struct P b; };
int main(void) {
  struct B b = { .a = {1, 2} };
  return b.b.x + b.b.y; // 期待: 0
}
// 期待: 0
