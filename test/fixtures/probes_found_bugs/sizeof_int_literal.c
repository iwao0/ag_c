// `sizeof(0)` 等、接尾辞なし整数リテラルの sizeof は int の 4。
// psx_node_type_size が ND_NUM に 0 を返し、sizeof_expr_node の既定 8 に落ちて
// `sizeof(0)`/`sizeof(42)` が 8 になっていた (`sizeof(-1)` は ND_SUB 経由で 4、
// `sizeof(0L)` は既定 8 が偶然正しかった)。sizeof_expr_node で ND_NUM を fp_kind と
// long サフィックスから判定して修正。
#include <assert.h>

int main(void) {
    assert(sizeof(0) == 4);
    assert(sizeof(42) == 4);
    assert(sizeof(-1) == 4);
    assert(sizeof('a') == 4);          // 文字定数は int
    assert(sizeof(0U) == 4);           // unsigned int も 4
    assert(sizeof(0L) == 8);
    assert(sizeof(0LL) == 8);
    assert(sizeof(0UL) == 8);
    assert(sizeof(0.0) == 8);          // double
    assert(sizeof(0.0f) == 4);         // float
    assert(sizeof(1 + 2) == 4);        // int 算術式
    assert(sizeof(1L + 2) == 8);       // long 混在
    return 0;
}
