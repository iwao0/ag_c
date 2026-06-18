// 関数のアドレス取得 `&f` が壊れていたバグ (init/代入とも ir_build_module failed)。
// build_unary_addr_node が ND_FUNCREF を ND_ADDR でラップしてしまい、IR builder が
// ND_ADDR(ND_FUNCREF) を扱えず失敗していた。C では `&f` は関数ポインタそのもの
// (= `f`) なので、ND_FUNCREF をそのまま返す。
// 修正前: ir_build_module failed
// 期待: exit=42
#include <assert.h>
int add(int a, int b) { return a + b; }
int main(void) {
    int (*fp)(int, int) = &add;       // &f で初期化
    int (*gp)(int, int);
    gp = &add;                        // &f で代入
    int r1 = fp(40, 2);               // 42
    int r2 = (*gp)(40, 2);            // 42 (&f 初期化 + 明示 deref 呼び出し)
    assert(r1 == 42);
    assert(r2 == 42);
    return 0;
}
