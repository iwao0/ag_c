// unsigned int (32bit) の演算結果が 2^32 で折り返さないバグ。
// codegen が整数演算を 64bit レジスタで行い結果を 32bit に切り詰めないため、
// 格納/キャストを経ない直接使用 (比較・条件) で値が壊れていた。
//   unsigned x=0xFFFFFFFF; (x+1)==0  → 期待 true だが false になっていた
// (`unsigned y=x+1; y==0` や `(int)(x+1)` は格納/キャストの切り詰めで動いていた)
// 修正: unsigned int の +,-,*,<< の結果を 0xFFFFFFFF でマスクして 32bit へ折り返す
// (C11 6.2.5p9)。符号付きは UB なので対象外、long(64bit) は元から正しい。
// 修正前: exit=0
// 期待: exit=42
#include <assert.h>
int main(void) {
    unsigned a = 0xFFFFFFFFu;
    unsigned b = 0x80000000u;
    int ok = ((a + 1) == 0) &&            // 加算: 桁上がりで 0 へ wrap
             ((a - (a + 5)) == 0xFFFFFFFBu) &&  // 減算: 借りで wrap
             ((a * 2) == 0xFFFFFFFEu) &&   // 乗算: 上位 32bit 切り捨て
             ((b << 1) == 0) &&            // 左シフト: bit31 が押し出され 0
             ((a >> 4) == 0x0FFFFFFFu) &&  // 右シフト (LSR) は元から正しい
             ((a + 1) < 5u);               // wrap 後の比較
    assert(ok);
    return 0;
}
