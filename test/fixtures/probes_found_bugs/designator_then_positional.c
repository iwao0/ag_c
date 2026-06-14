// 指定初期化子 (`.member=val`) の後に続く位置指定初期化子が、誤ったメンバに
// 代入されていたバグ。C11 6.7.9p17 では designator の後の位置指定は、その
// designated member の「次」のメンバから順に継続する。
// 原因: struct 初期化ループで `.member` を処理しても位置指定用の ordinal を
//   同期しておらず、後続の位置指定が先頭付近のメンバを誤って上書きしていた。
// 修正: designated member の index を求め、ordinal を index+1 に同期する。
// 修正前: `{.b=2, 3, 4}` で 3,4 が誤配置 (agc=72 など)
// 期待: exit=42
struct Quad { int a, b, c, d; };
struct Tri  { int x, y, z; };

int main(void) {
    // .b の後、3→c, 4→d
    struct Quad q = {.b = 2, 3, 4};
    if (q.a != 0 || q.b != 2 || q.c != 3 || q.d != 4) return 1;

    // 先頭に designator、以降位置指定で継続
    struct Tri t = {.x = 1, 2, 3};            // x=1, y=2, z=3
    if (t.x != 1 || t.y != 2 || t.z != 3) return 2;

    // 位置指定 → designator → 位置指定継続
    struct Quad r = {10, .c = 30, 40};        // a=10, b=0, c=30, d=40
    if (r.a != 10 || r.b != 0 || r.c != 30 || r.d != 40) return 3;

    // 複数 designator 間に位置指定
    struct Quad s = {.a = 1, 2, .d = 4};      // a=1, b=2, c=0, d=4
    if (s.a != 1 || s.b != 2 || s.c != 0 || s.d != 4) return 4;

    return q.c + q.d + t.z + r.c + s.b;       // 3 + 4 + 3 + 30 + 2 = 42
}
