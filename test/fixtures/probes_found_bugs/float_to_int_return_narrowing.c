// 続き50: 整数戻り型の関数から float を return する縮小変換の検出 (W3010 拡張)。
//
// parse_stmt_return で node->lhs の fp_kind が float/double で関数戻り型が整数なら警告。
// 続き44/49 で init/assign に対応した既存の W3010 を return 経路にも拡張した形。
//
// 抑制条件:
//   - `return 2.0;` (整数値リテラル): 値が変わらないので警告しない
//   - `return (int)d;` (明示キャスト): apply_cast 後の fp_kind が NONE
//   - float/double 戻り型の関数: 縮小ではないので警告しない
//   - void 戻り型: そもそも値を返せないので別系統エラー
//
// 本 fixture は合法形 (警告が出てはいけない形) のみを含む。
#include <assert.h>

/* (a) 整数値の float リテラル return — 警告なし */
int return_integer_valued_float(void) {
    return 2.0;
}

/* (b) 明示キャストでの float→int return — 警告なし */
int return_with_explicit_cast(void) {
    double d = 3.7;
    return (int)d;
}

/* (c) float 戻り型の関数では float 値を返しても警告なし */
double return_float_from_float_fn(void) {
    double d = 1.5;
    return d;
}

float return_literal_to_float_fn(void) {
    return 2.5f;
}

/* (d) 整数戻り型に整数値を return — 当然警告なし */
int return_int_from_int_fn(void) {
    int x = 42;
    return x;
}

/* (e) 整数→float への return は縮小ではないので警告なし (拡張) */
double widening_return(void) {
    int i = 5;
    return i;  /* int → double (実装定義の拡張、縮小ではない) */
}

int main(void) {
    assert(return_integer_valued_float() == 2);
    assert(return_with_explicit_cast() == 3);
    double f = return_float_from_float_fn();
    assert(f > 1.0 && f < 2.0);
    float ff = return_literal_to_float_fn();
    assert(ff > 2.0f && ff < 3.0f);
    assert(return_int_from_int_fn() == 42);
    double w = widening_return();
    assert(w > 4.0 && w < 6.0);
    return 0;
}
