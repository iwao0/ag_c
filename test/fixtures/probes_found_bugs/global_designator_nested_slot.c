// グローバルの指定初期化子 (designator) の flat slot 計算が、入れ子 struct を
// 1 slot としか数えず壊れていた 3 つのバグ。
// (1) `.member=` の slot が、先行する入れ子 struct メンバを 1 slot として数えて
//     いた。`struct Out{struct In i; int z;}` で z の slot が 1 (本来 2) になり、
//     `{.i={..}, .z=14}` の .z が i.q を上書きしていた。
// (2) `.member = {...}` の入れ子 brace が、designator の slot でなく init_count から
//     書き始めていた。`{.z=14, .i={12,13}}` の .i が後方ジャンプできず化けた。
// (3) struct 要素配列の `[N]=` が N を flat slot とみなし、要素の内側スカラ数で
//     スケールしていなかった。`struct P g[3]={[2]={5,6}}` の [2] が slot 2 (本来 4)。
// 修正: 入れ子 struct のフラットスロット数を再帰的に数えて slot を計算し、入れ子
//   brace は外側 cur_idx から書き始め、`[N]=` は要素スロット数を掛ける。
// 期待: exit=42
struct In  { int p, q; };
struct Out { struct In i; int z; };
struct P   { int x, y; };

struct Out g1 = {.i = {1, 2}, .z = 3};        // (1)(2) i.p=1,i.q=2,z=3
struct Out g2 = {.z = 6, .i = {4, 5}};        // (2) 逆順 designator
struct P   g3[3] = {[2] = {7, 8}, [0] = {9, 10}}; // (3) struct 配列 designator

int main(void) {
    if (g1.i.p != 1 || g1.i.q != 2 || g1.z != 3) return 1;
    if (g2.i.p != 4 || g2.i.q != 5 || g2.z != 6) return 2;
    if (g3[0].x != 9 || g3[0].y != 10) return 3;
    if (g3[1].x != 0 || g3[1].y != 0) return 4;   // 未指定要素は 0
    if (g3[2].x != 7 || g3[2].y != 8) return 5;

    return g1.z + g2.z + g3[2].x + g3[0].y + g3[2].y + 8;  // 3+6+7+10+8+8 = 42
}
