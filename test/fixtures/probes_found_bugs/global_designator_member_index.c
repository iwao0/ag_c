// グローバル集約初期化子の `.member[idx]` / `.member.sub` / `.arr[idx].field`
// designator チェーン (C11 6.7.9p6)。旧実装の global flat パーサは `.member` の直後に
// `=` を要求し、続く `[idx]` や `.sub` を E2006/E3064 で拒否していた。ローカルは
// 対応済みだったがグローバルが未対応だった。flat パーサに [idx]/.sub チェーンの
// slot 計算を追加して修正。
struct P { int x, y; };
struct Inner { int a, b; };
struct A  { int n; int arr[3]; };
struct B  { struct P pt; int vals[3]; };
struct C  { struct Inner items[3]; int tag; };

// .arr[idx] = scalar
struct A ga = { .n = 1, .arr[1] = 7, .arr[2] = 9 };
// .member.sub + .arr[idx] mixed
struct B gb = { .pt.x = 3, .pt.y = 4, .vals[0] = 10, .vals[2] = 30 };
// .arr[idx].field chained
struct C gc = { .items[2].a = 7, .items[0].b = 3, .items[2].b = 8, .tag = 99 };

int main(void) {
  int r = 0;

  if (ga.n != 1 || ga.arr[0] != 0 || ga.arr[1] != 7 || ga.arr[2] != 9) r |= 1;
  if (gb.pt.x != 3 || gb.pt.y != 4) r |= 2;
  if (gb.vals[0] != 10 || gb.vals[1] != 0 || gb.vals[2] != 30) r |= 4;
  if (gc.items[0].a != 0 || gc.items[0].b != 3) r |= 8;
  if (gc.items[2].a != 7 || gc.items[2].b != 8) r |= 16;
  if (gc.items[1].a != 0 || gc.tag != 99) r |= 32;

  return r == 0 ? 42 : r;
}
