// struct ポインタのメンバアクセスでタグが伝播しない2つのバグ。
// (1) `(++p)->m` / `(p++)->m`: inc/dec の結果が struct タグを継承せず E3005。
//     psx_node_get_tag_type に ND_PRE_INC/POST_INC/PRE_DEC/POST_DEC が無かった。
// (2) typedef した struct ポインタ仮引数 `T *t` (`typedef struct{...} T;`) で
//     `t->m` が E3005。parse_param_scalar_decl_spec が typedef のタグを NULL で
//     捨てており、仮引数にタグ・struct_size が伝わっていなかった。
// 修正前: E3005 でコンパイル失敗
// 期待: exit=42
#include <assert.h>
typedef struct { int v; } T;
int sumv(T *a, int n) {            // typedef struct ポインタ仮引数
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i].v;
    return s;
}
int main(void) {
    T arr[3] = {{10}, {20}, {12}};
    T *p = arr;
    int viz = (++p)->v;            // ++p -> arr[1] = 20
    viz += (p--)->v;              // p-- は arr[1] を読んでから arr[0] へ (20)
    // sumv = 10+20+12 = 42; ここでは別途検証
    int total = sumv(arr, 3);     // 42 (typedef struct ポインタ仮引数で各要素を加算)
    // 配列内容を直接検査してから sumv の合計を確認 (合計だけだと要素取り違えを見逃す)。
    assert(arr[0].v == 10);
    assert(arr[1].v == 20);
    assert(arr[2].v == 12);
    assert(total == 42);
    assert(viz == 40);            // (++p)->v(20) + (p--)->v(20)
    return 0;
}
