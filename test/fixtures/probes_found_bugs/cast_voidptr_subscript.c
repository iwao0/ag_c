// `((int*)void_p)[i]` のようにインラインで void* を要素型ポインタへキャストして添字する形。
// apply_cast の void* キャスト分岐が deref_size をキャスト先要素サイズに更新せず、
// 既定の 8 バイトストライドで添字して誤った要素を読んでいた (`((int*)v)[1]` が a[2])。
// deref_size を cast 要素サイズに設定して修正 (base_deref_size は立てない=結果はスカラ)。
#include <assert.h>

int main(void) {
    int a[3] = {10, 20, 30};
    void *v = a;
    assert(((int *)v)[0] == 10);
    assert(((int *)v)[1] == 20);   // 以前は a[2]=30 を読んでいた
    assert(((int *)v)[2] == 30);

    // 書き込みも正しいストライド
    ((int *)v)[1] = 99;
    assert(a[1] == 99);

    // long* 経由 (8 バイトストライド)
    long arr[2] = {100, 200};
    void *lp = arr;
    assert(((long *)lp)[0] == 100);
    assert(((long *)lp)[1] == 200);

    // short* 経由 (2 バイトストライド)
    short s[3] = {1, 2, 3};
    void *sp = s;
    assert(((short *)sp)[2] == 3);
    return 0;
}
