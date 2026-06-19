// 関数定義の戻り値型に inline union タグ (パース確認のみ)
// 期待: exit=0
#include <assert.h>
union U { int x; } f(void) {
    union U u;
    return u;
}
int main(void) { assert(0 == 0); return 0; }
