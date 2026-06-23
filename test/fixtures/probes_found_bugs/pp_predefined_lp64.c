// プリプロセッサ定義済みマクロ __LP64__ (Apple ARM64 は LP64 データモデル)。
// 未定義だと c-testsuite 00212 が "KO no __*LP*__ defined." を出力する。
#include <assert.h>
#include <stdio.h>

int main(void) {
#if defined(__LP64__)
    assert(sizeof(short) == 2);
    assert(sizeof(int) == 4);
    assert(sizeof(long) == 8);
    assert(sizeof(long long) == 8);
    assert(sizeof(void *) == 8);
    printf("Ok\n");
#else
    printf("KO no __*LP*__ defined.\n");
    return 1;
#endif
    return 0;
}
