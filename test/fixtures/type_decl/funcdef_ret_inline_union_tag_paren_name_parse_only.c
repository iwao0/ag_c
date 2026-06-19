// 関数名を () で囲んだ inline union タグ戻り値 (パース確認)
// 期待: exit=0
#include <assert.h>
union U { int x; } (f)(void) {
    union U u;
    return u;
}
int main(void) { assert(0 == 0); return 0; }
