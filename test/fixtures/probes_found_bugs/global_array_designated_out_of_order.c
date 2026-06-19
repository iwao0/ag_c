// グローバル配列の「順不同」designated 初期化で要素が欠落するバグ。
// 書き込み位置を init_count (増加のみ) で追跡していたため、designator が後方
// ジャンプ (`[4]=` の後に `[1]=`) すると書き込みが末尾に流れ、対象 index に
// 反映されなかった。
// 修正: 書き込み位置を cur_idx (フラット絶対 index) で追跡し、init_count は
//       充填済み最大数として扱う。ネスト brace は init_count から再開する。
// 修正前: exit=10 ([1]=20 が欠落)
// 期待: exit=42
#include <assert.h>
int g[5] = {[4] = 10, [1] = 20, [3] = 12};   // 降順/順不同 designator
int main(void) {
    int sum = g[0] + g[1] + g[2] + g[3] + g[4];   // 0+20+0+12+10 = 42
    assert(g[0] == 0); assert(g[1] == 20); assert(g[2] == 0); assert(g[3] == 12); assert(g[4] == 10); assert(sum == 42); return 0;
}
