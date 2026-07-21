// `extern T var;` で宣言のみのグローバル変数 (定義は別 TU、典型は libc の `__stderrp` 等)
// が @PAGE/@PAGEOFF 直参照されてリンク時に「does not have address」で失敗していた回帰。
//
// 修正: generic IRでexternal linkageを保持し、Apple backendがGOT参照を選択する。
// codegen はこれを見て関数アドレスと同じ @GOTPAGE/@GOTPAGEOFF 経由で解決する。
//
// 副次効果: stdio.h に stderr/stdout/stdin の extern 宣言を追加した
// (Apple libc が __std{in,out,err}p としてエクスポート)。
#include <stdio.h>
#include <assert.h>

int main(void) {
    /* fprintf(stderr, ...) を試みる。出力先は stderr なので stdout 比較には影響しない
     * が、リンクが通り実行できれば OK。 */
    int n = fprintf(stderr, "extern_global_got test ok\n");
    assert(n > 0);
    /* stdout も使えるか */
    int n2 = fprintf(stdout, "stdout=%d\n", 42);
    assert(n2 > 0);
    return 0;
}
