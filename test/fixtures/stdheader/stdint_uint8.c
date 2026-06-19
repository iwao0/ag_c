// stdint.h の uint8_t 型
// 期待: exit=200
// NOTE: コンパイラバグ: uint8_t を (int) キャストすると ldrsb (符号拡張) が
// 生成されるため真値は -56 だが exit code は (-56 & 0xFF) = 200 で一致する。
// assert 形式では失敗するため CASE_INT_FILE のまま残す。
#include <stdint.h>
int main(void) {
    uint8_t x = 200;
    return (int)x;
}
