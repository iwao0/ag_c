// グローバル char 配列を文字列で初期化
#include <assert.h>
char msg[] = "hi";
int main(void) {
  assert(msg[0] == 'h'); assert(msg[1] == 'i'); return 0;
}
// 期待: 'h'(104) + 'i'(105) = 209
