// 関数ポインタの明示 deref 呼び出し `(*fp)(args)` が壊れていたバグ。
// callee が ND_DEREF(fp) になり、IR builder が関数アドレス [fp] をロード (= 関数
// コード先頭を値として読む) して呼び出していた。C では `*fp` は関数へ戻り即座に
// 関数ポインタへ減衰するので `fp(args)` と等価。
// 修正: call の callee が「単項 deref の連なりで最下層が関数ポインタ lvar
// (pointer_qual_levels<=1)」なら全段剥がして lvar を直接 callee にする。subscript
// (`ops[i]`) やポインタ→fp (`(*pp)`, pql>=2) の実体 deref は除外。
// 修正前: garbage (関数コードを呼ぶ)
// 期待: exit=42
#include <assert.h>
int sub(int a, int b) { return a - b; }
int add(int a, int b) { return a + b; }
int main(void) {
    int (*fp)(int, int) = sub;
    int (*ops[2])(int, int) = {add, sub};
    int (**pp)(int, int) = &fp;
    int r1 = (*fp)(50, 8);        // sub(50,8) = 42
    int r2 = (**fp)(50, 8);       // 同上 = 42 (多段 deref も畳む)
    int r3 = ops[0](40, 2);       // add(40,2) = 42 (subscript は据え置き)
    int r4 = (*pp)(50, 8);        // *pp = fp -> sub(50,8) = 42
    assert(r1 == 42);
    assert(r2 == 42);
    assert(r3 == 42);
    assert(r4 == 42);
    return 0;
}
