// 名前付き仮引数を持つ可変引数関数定義 (受理確認)
// 期待: exit=9
#include <assert.h>
int pick(int tag, ...) { return tag + 9; }
int main(void) { assert(pick(0) == 9); return 0; }
