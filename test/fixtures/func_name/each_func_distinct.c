// 関数ごとに __func__ が独立した文字列を返すこと
// fa[1]='a'=97, fb[1]='b'=98 を個別に検査
#include <assert.h>
int fa(void) { return (int)__func__[1]; }
int fb(void) { return (int)__func__[1]; }
int main(void) {
    assert(fa() == 'a');
    assert(fb() == 'b');
    return 0;
}
