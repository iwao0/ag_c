// 続き63: `typedef int Row[3]; struct Mat { Row r[2]; };` の宣言子側 [N] と
// typedef 側 [M] の組合せ (修正)。
//
// 修正前: struct_layout の typedef array 取り込みが「宣言子に追加 [N] なし」のときだけ
// 動作し、両方ある形 (`Row r[2]`) は宣言子側だけで arr_dim_count=1 / dims=[2] となり、
// typedef 側の [3] が落ちて Row がスカラ扱いされる。波括弧初期化が「スカラに複数要素」
// で E3064 reject。
//
// 修正: 宣言子側 dims を outer、typedef 側 dims を inner に連結する。
// (`r[2]` declarator + `Row [3]` typedef = `r[2][3]` の 2D 配列、6 要素)。
#include <assert.h>

typedef int Row3[3];

struct M {
    Row3 r[2];     /* 2x3 int (= 24 バイト) */
    int trailing;  /* 追加スカラメンバで stride 計算ミスを検出 */
};

int main(void) {
    /* 1 段 typedef + 宣言子追加 1 次元 = 2D 配列メンバ */
    struct M m = { { {1, 2, 3}, {4, 5, 6} }, 999 };
    assert(m.r[0][0] == 1);
    assert(m.r[0][2] == 3);
    assert(m.r[1][0] == 4);
    assert(m.r[1][2] == 6);
    assert(m.trailing == 999);  /* 配列領域を超えて隣メンバを壊していないこと */
    assert(sizeof(m.r) == sizeof(int) * 2 * 3);

    /* 個別書き込み */
    m.r[1][1] = 42;
    assert(m.r[1][1] == 42);
    assert(m.r[0][1] == 2);  /* 隣行を破壊していない */
    assert(m.trailing == 999);  /* 越境していない */

    return 0;
}
