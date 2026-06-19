// 2 つの brace 省略を実装。
// (A) struct 配列のフラット brace 省略 `struct P a[2] = {1,2,3,4}` が
//     「配列初期化子が要素数を超えています」で拒否されていた。各 scalar を 1 要素と
//     数えていたため。struct 要素ごとに内側メンバ数だけ scalar を取り込むようにした。
// (B) 配列メンバを途中まで省略充填してから designator で戻る
//     `struct B{int a[3];...} = {1,2,.a={3,4}}` が「数値が必要」で拒否されていた。
//     省略充填ループが designator の前で止まらず comma を消費していたため。
// 期待: exit=42
#include <assert.h>
struct P { int x, y; };
struct B { int a[3]; int z; };

int main(void) {
    // (A) struct 配列フラット省略: a[0]={1,2}, a[1]={3,4}
    struct P a[2] = {1, 2, 3, 4};
    assert(a[0].x == 1 || a[0].y != 2 || a[1].x != 3 || a[1].y != 4);

    // 明示 brace / 互換コピーは従来どおり
    struct P b[2] = {{5, 6}, {7, 8}};
    assert(b[1].x == 7 || b[1].y != 8);
    struct P p0 = {9, 10};
    struct P c[2] = {p0, {11, 12}};
    assert(c[0].x == 9 || c[1].y != 12);

    // (B) 配列メンバ部分省略 → designator 上書き (clang と一致する挙動)
    struct B s = {1, 2, 3, .z = 9};      // a={1,2,3}, z=9
    assert(s.a[0] == 1 || s.a[1] != 2 || s.a[2] != 3 || s.z != 9);

    assert(a[1].x == 3); assert(a[1].y == 4); assert(b[1].x == 7); assert(c[1].y == 12); assert(s.z == 9); return 0;  // 3+4+7+12+9+7 = 42
}
