// struct メンバが struct の配列のとき (`struct P pts[N];`)、その要素を brace で
// 初期化できなかった (ローカルは E3064、グローバルは各要素を struct サイズの
// スカラとして出力して化けていた)。
//   local : parse_member_initializer の配列パスが要素を parse_scalar_brace_initializer
//           で処理 → struct 要素 `{1,2}` を拒否。struct 要素は parse_struct_initializer
//           に委譲するよう修正。
//   global: emit_global_struct_members_rec の配列メンバ出力が要素を ts バイトの
//           スカラとして 1 つ出力 → 各要素 struct を再帰展開するよう修正。
struct Point { int x, y; };
struct Shape { struct Point pts[3]; int color; };

// グローバル: positional ネスト brace と designated メンバ
struct Shape gshape = { .pts = {{10, 20}, {30, 40}}, .color = 5 };

int main(void) {
  int t = 0;

  // グローバル array-of-struct メンバ
  t += gshape.pts[0].x + gshape.pts[0].y;   // 30
  t += gshape.pts[1].x + gshape.pts[1].y;   // 70
  t += gshape.pts[2].x + gshape.pts[2].y;   // 0 (zero-fill)
  t += gshape.color;                        // 5

  // ローカル: designated 要素 + 内側 struct designator
  struct Shape ls = { .pts = {[0] = {.x=1, .y=2}, [2] = {.x=5, .y=6}}, .color = 7 };
  t += ls.pts[0].x + ls.pts[0].y;           // 3
  t += ls.pts[1].x + ls.pts[1].y;           // 0
  t += ls.pts[2].x + ls.pts[2].y;           // 11
  t += ls.color;                            // 7

  // ローカル: positional ネスト brace + 部分初期化のゼロfill
  struct Shape ps = { .pts = {{1, 1}}, .color = 2 };
  t += ps.pts[0].x + ps.pts[0].y;           // 2
  t += ps.pts[1].x + ps.pts[2].y;           // 0
  t += ps.color;                            // 2

  return t - 88;  // total 130 ; 130-88 = 42
}
