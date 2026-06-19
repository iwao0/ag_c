// switch の case ラベルに INT_MAX を超える long 定数を使うと拒否されるバグ。
// C11 6.8.4.2: case の式は整数定数式であればよく、int に収まる必要はない
// (制御式の昇格後の型で比較される)。
// 定数式評価は long long で動くが、末端 parse_primary が tk_expect_number()
// 経由で int に切り詰めるため、5000000000L が E2007 で拒否されていた。
// switch 本体の比較は 64bit 対応済みなので、parse さえ通れば正しく動く。
// 修正前: E2007 "必要な整数がありません" でコンパイル失敗
// 期待: exit=42
#include <assert.h>
int main(void) {
    long x = 5000000000L;
    switch (x) {
        case 5000000000L:
            assert(1); return 0;
        default:
            return 0;
    }
}
