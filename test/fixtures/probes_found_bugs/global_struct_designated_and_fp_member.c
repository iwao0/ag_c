// グローバル struct/union 初期化の2つの関連バグ。
// (1) `.member =` designated 初期化が E3064 で拒否されていた (struct/union とも、
//     flatten が `[N]` のみ対応で `.member` を扱わなかった)。
// (2) struct/union の float/double メンバが整数部のビットで出力され値が壊れていた
//     (init_fvalues が tagged 型で確保されず、emit も fp を無視していた)。
// 修正: flatten が `.member` designator を解決し flat slot/union 活性メンバへ反映、
//       init_fvalues を struct/union でも確保、emit が fp メンバをビットで出力。
// 修正前: コンパイル失敗 または 値破損
// 期待: exit=42
struct S { int a; float f; int b; };
struct S gs = {.f = 3.5f, .b = 7};      // a=0, f=3.5, b=7 (designated + fp member)

union U { int i; float f; };
union U gu = {.f = 1.5f};                // 活性メンバ f = 1.5

int main(void) {
    int s = (int)(gs.f * 10) + gs.b + gs.a;   // 35 + 7 + 0 = 42
    int u_ok = ((int)(gu.f * 10) == 15);       // 1.5 * 10 = 15
    return (s == 42 && u_ok) ? 42 : 0;
}
