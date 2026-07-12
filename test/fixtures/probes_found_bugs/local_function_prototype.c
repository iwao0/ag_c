// 続き77: 関数内ローカル関数プロトタイプ宣言 (修正)。c-testsuite 00078 由来。
//
// 修正前: 関数内で `int f1(char *);` のような関数プロトタイプ宣言が「ローカル変数 f1」
// として登録され、呼び出しが「ローカルスロットを load して間接呼び出し」になっていた。
// 結果、初期化されていないスタックスロットを関数ポインタとして bl していたため SIGSEGV。
// 警告は「初期化されていない変数 'f1' が使用されています」が出ていたが症状の本質を見落としていた。
//
// 修正: local declaration の declarator 解析直後に
// 「declarator が non-pointer の関数 (`(...)` 付きで `*` なし)」を検出し、ローカル変数として
// 登録せず宣言を読み飛ばすだけにする (C11 6.2.2p5: 関数内関数宣言は暗黙 extern)。
// 既存のグローバル関数定義はそのまま使われ、通常の関数呼び出し経路 (bl _f1) で解決される。
//
// 関数ポインタ変数 (`int (*fp)(char *);`) は is_pointer=1 なので除外され、従来どおりローカル
// 変数として登録される。
#include <assert.h>

int f1(char *p) { return *p + 1; }
int f2(int a, int b) { return a + b; }
int f3(int a) { return a + 3; }
int extern_seed = 39;

int main(void) {
    char s = 1;
    int v[100];  /* スタック消費させ、もし誤ったスロットがあれば SIGSEGV を誘発 */
    int f1(char *);              /* ローカル関数 prototype */
    int f2(int, int);            /* 引数 2 個も */
    extern int f3(int), extern_seed; /* 明示 extern + 変数宣言の混在 */

    assert(f1(&s) == 2);
    assert(f2(3, 4) == 7);
    assert(f3(extern_seed) == 42);

    /* 関数ポインタ変数は従来どおりローカル変数 (回帰確認) */
    int (*fp)(char *) = f1;
    assert(fp(&s) == 2);

    return 0;
}
