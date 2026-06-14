// ネスト struct メンバの brace 省略初期化 (C11 6.7.9p17-20) が未実装で、
// `struct Outer o = {1, 2, 3};` (1,2 が内側 struct へ、3 が次メンバへ) が
// E3064 (構造体の単一式初期化は同型オブジェクトのみ) で拒否されていた。
// 実装: `{` 無しの struct メンバ初期化で、scalar 始まりなら brace 省略として
// 親のフラットリストから内側 struct の名前付きメンバを宣言順に取り込む。各メンバを
// parse_member_initializer で処理するため入れ子集約も再帰的に展開される。
// 互換 struct 式 (識別子/`(` 始まり) はコピー初期化を維持。
// 期待: exit=42
struct In  { int p, q; };
struct Out { struct In i; int z; };
struct A   { int x, y; };
struct B   { struct A a; int z; };
struct C   { struct B b; int w; };

int main(void) {
    // 1 段ネスト brace 省略
    struct Out o = {1, 2, 3};                 // i.p=1, i.q=2, z=3
    if (o.i.p != 1 || o.i.q != 2 || o.z != 3) return 1;

    // 明示 brace も従来どおり
    struct Out o2 = {{4, 5}, 6};              // i.p=4, i.q=5, z=6
    if (o2.i.p != 4 || o2.i.q != 5 || o2.z != 6) return 2;

    // 3 段ネスト brace 省略
    struct C c = {1, 2, 3, 4};                // c.b.a.x=1, c.b.a.y=2, c.b.z=3, c.w=4
    if (c.b.a.x != 1 || c.b.a.y != 2 || c.b.z != 3 || c.w != 4) return 3;

    // 部分指定 (残りは 0)
    struct Out o3 = {7};                      // i.p=7, i.q=0, z=0
    if (o3.i.p != 7 || o3.i.q != 0 || o3.z != 0) return 4;

    // 互換 struct 変数からのメンバコピー
    struct In src = {8, 9};
    struct Out o4 = {src, 10};                // i=src, z=10
    if (o4.i.p != 8 || o4.i.q != 9 || o4.z != 10) return 5;

    // brace 省略 + designator 併用
    struct Out o5 = {1, 2, .z = 30};          // i.p=1, i.q=2, z=30
    if (o5.i.p != 1 || o5.i.q != 2 || o5.z != 30) return 6;

    return o.i.p + o.i.q + o.z + c.w + o3.i.p + o4.z + 15;  // 1+2+3+4+7+10+15 = 42
}
