// 浮動小数の単項マイナスが `0.0 - x` で実装されており、-0.0 の符号ビットを生成できず
// 値が化けていたバグ。`1.0 / -0.0` が +inf (本来 -inf)、`-0.0` のビットが +0.0 等。
// 原因: unary() が単項マイナスを ND_SUB(0, x) にしていた。IEEE では `0.0 - (+0.0)` = +0.0
//      だが `-(+0.0)` (符号反転) は -0.0。IR_FNEG 命令は存在したが ir_builder が未使用。
// 修正: Typed HIR の PSX_HIR_NEGATE を IR_FNEG (符号ビット反転) に変換。
//      整数は従来どおり `0 - x`。
// 修正前: -0.0 が +0.0 になり 1.0/-0.0 が +inf
// 期待: exit=42
#include <assert.h>
int main(void){
    double nz = -0.0;
    double pz = 0.0;
    assert(1.0 / nz < -1e300);     // 1.0 / -0.0 = -inf
    assert(1.0 / pz > 1e300);      // 1.0 / +0.0 = +inf
    assert(nz == pz);              // -0.0 == 0.0 (値比較は等しい)
    double z = 0.0;
    assert(1.0 / -z < -1e300);     // 実行時 negate も -0.0
    // 通常の負号は不変
    double a = 3.5;
    assert(-a == -3.5);
    float f = 2.25f;
    assert(-f == -2.25f);
    int i = 5;
    assert(-i == -5);              // 整数経路
    return 0;
}
