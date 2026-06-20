// B6: ファイルスコープでスカラ複合リテラルのアドレスを取る `int *p = &(int){5};`
// が miscompile していた。ファイルスコープのスカラ複合リテラルは値 (ND_NUM) に短絡され、
// `&` が「数値リテラルのアドレス」になりアドレスを解決できず、グローバルが `.comm`
// (ゼロ初期化) で出力 → `*p` が NULL deref で SIGSEGV していた。
// C11 6.5.2.5: ファイルスコープの複合リテラルは静的記憶域期間を持つので、静的実体を
// 生成しそのアドレスで初期化する。整数/long/char/unsigned/double/float と、ポインタ配列
// 要素での `&複合リテラル` を網羅。値文脈 `(int){10}` が退行しないことも確認する。
#include <assert.h>

int      *pi = &(int){10};
long     *pl = &(long){100};
char     *pc = &(char){3};
double   *pd = &(double){2.5};
unsigned *pu = &(unsigned){7};
float    *pf = &(float){4.5f};

int *arr[] = { &(int){1}, &(int){2}, &(int){3} };

// 値文脈 (アドレス取得なし) の複合リテラル: gvar 実体化させず値として扱う (退行防止)。
int vals[3] = { (int){4}, (int){5}, (int){6} };

int main(void) {
    assert(*pi == 10);
    assert(*pl == 100);
    assert(*pc == 3);
    assert(*pd == 2.5);
    assert(*pu == 7);
    assert(*pf == 4.5f);
    assert(*arr[0] == 1 && *arr[1] == 2 && *arr[2] == 3);
    // 値文脈は退行していないこと。
    assert(vals[0] == 4 && vals[1] == 5 && vals[2] == 6);
    return 0;
}
