// struct ポインタのタグ伝播 2 件。
// (1) `(p += n)->m` / `(p -= n)->m`: 複合代入の結果 (ND_ASSIGN) が左辺のタグを
//     継承せず E3005。psx_node_get_tag_type の ND_ASSIGN で tag が空なら lhs から継承。
// (2) `(*pp)->m` (pp は struct N**): 単項 `*` の結果が依然 struct ポインタなのに
//     is_tag_pointer=0 にされ E3005。多段ポインタ (pql>=2) なら is_tag_pointer を維持。
// 修正前: E3005 でコンパイル失敗
// 期待: exit=42
#include <assert.h>
struct N { int v; };
int main(void) {
    struct N arr[3] = {{10}, {20}, {12}};
    struct N *p = &arr[0];
    int a = (p += 2)->v;        // arr[2] = 12
    int b = (p -= 1)->v;        // arr[1] = 20
    struct N n = {10};
    struct N *q = &n;
    struct N **pp = &q;
    int c = (*pp)->v;           // 10
    assert(a == 12);   // (p += 2)->v
    assert(b == 20);   // (p -= 1)->v
    assert(c == 10);   // (*pp)->v
    return 0;
}
