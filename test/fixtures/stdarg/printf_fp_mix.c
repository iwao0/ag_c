// printf で int / double / 文字列を混在して渡す。
// printf も Apple ARM64 variadic ABI 経由に統一されているので、
// 個別の特殊コード生成パスを通らないことを確認する。
// 出力結果は cc 標準ライブラリ printf が解釈するので、
// 正しく stack に積まれている == 出力が乱れない、で確認する。
// 期待: exit=0 (printf 自体は 0 以外を返す可能性があるが、main で 0 を返す)
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("d=%d f=%f s=%s d=%d f=%f\n", 1, 2.5, "x", 7, 8.5);
    return 0;
}
