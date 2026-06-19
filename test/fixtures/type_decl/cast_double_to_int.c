// (int) で double から int への明示キャスト (fcvtzs)
// 修正前: apply_cast が fp_kind を NONE にするだけで、double のビット
//        パターンを int として読んでいたため (int)7.0 が 0 になっていた
// 対応: ND_FP_TO_INT ノードを新設し、codegen で fcvtzs x0, d0 を emit
// 期待: exit=7
#include <assert.h>
int main(void) {
    double d = 7.0;
    assert((int)d == 7);
    return 0;
}
