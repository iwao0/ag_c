// 続き46: `if (a, b)` / `while (a, b)` の条件式に comma 演算子を使う形を W3001 警告
// (clang -Wunused-value 相当)。`&&` のタイプミスが典型。
//
// parse_stmt_if / parse_stmt_while の warn_if_assign_as_condition で
// 条件式 top が ND_COMMA なら警告するよう拡張した。
//
// 本 fixture は false-positive 確認用 (合法形に warning が出ないこと)。
#include <assert.h>

int g_count = 0;

int side_effect(int v) {
    g_count++;
    return v;
}

int main(void) {
    /* 正常な比較は警告なし */
    int x = 1, y = 2;
    if (x < y) {} else { return 1; }

    /* &&/|| は警告なし (ND_LAND/ND_LOR) */
    if (x && y) {} else { return 2; }
    if (x || y) {} else { return 3; }

    /* for ループの init / update に comma を使うのは合法 (条件式ではない) */
    int sum = 0;
    for (int i = 0, j = 10; i < 5; i++, j--) {
        sum += i + j;
    }
    assert(sum == 0+1+2+3+4 + 10+9+8+7+6);

    /* comma を関数引数の区切りで使うのは ND_COMMA を生成しない (引数リスト) */
    int r = side_effect(1) + side_effect(2);
    assert(r == 3);
    assert(g_count == 2);
    return 0;
}
