// 続き49: 浮動小数→整数の縮小変換警告の拡張 (W3010)。
//
// 続き44 で既に対応していた `int x = 1.5;` (整数変数の小数値リテラル初期化) に加え:
//   (1) `int z = d;` のような double 変数からの初期化 (clang -Wfloat-conversion)
//   (2) `int w; w = 1.5;` のような代入文での float リテラル → int
//        (clang -Wliteral-conversion)
//   (3) `int w; w = d;` のような代入文での float 変数 → int (clang -Wfloat-conversion)
// を W3010 で検出するようにした。
//
// 抑制条件 (合法形):
//   - `int y = 2.0;` (整数値の浮動小数点リテラル): 値が変わらないため警告しない
//   - `int x = (int)d;` (明示キャスト): apply_cast 後の fp_kind が NONE なので警告しない
//   - `double e; e = 2.5;` (float→float): 縮小変換ではないので警告しない
//   - `*p = 1.5;` 等のポインタ宛先: ポインタ宛先は別経路でエラーになる
//
// 本 fixture は合法形 (警告が出てはいけない形) を網羅。
#include <assert.h>

int main(void) {
    /* (a) 整数値の浮動小数点リテラル — 警告なし */
    int y1 = 2.0;
    int y2 = 0.0;
    long ly = 100.0;
    assert(y1 == 2);
    assert(y2 == 0);
    assert(ly == 100);

    /* (b) 明示キャストでの float→int — 警告なし */
    double d = 3.7;
    int casted_init = (int)d;
    int casted_assign;
    casted_assign = (int)d;
    assert(casted_init == 3);
    assert(casted_assign == 3);

    /* (c) float→float の代入/初期化 — 縮小変換ではないので警告なし */
    double e1 = 1.5;
    double e2;
    e2 = 2.5;
    assert(e1 > 1.0 && e1 < 2.0);
    assert(e2 > 2.0 && e2 < 3.0);

    /* (d) int→int (size 縮小ではあるが浮動小数点ではない) — float-to-int warning は出ない */
    int i = 100;
    short s;
    s = (short)i;
    assert(s == 100);

    /* (e) ポインタ宛先 — 別経路 (ここでは扱わない、float 代入はそもそも型エラー) */
    int v = 5;
    int *p = &v;
    *p = 10;
    assert(v == 10);

    return 0;
}
