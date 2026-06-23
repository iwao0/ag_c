// 続き59: 配列 typedef を struct メンバに使ったときの初期化・アクセス対応
// (機能対応、修正前は E3064 でコンパイル不可)。
//
// `typedef int Row[4]; struct S { Row r; };` のような配列 typedef は struct_layout で
// 次元情報が無視され、メンバが arr_size=1 のスカラ扱いされていた。波括弧初期化が
// 「スカラ初期化子に複数要素」と判断され E3064 で reject されていた。
//
// 修正: typedef-name 分岐で psx_typedef_info_t::is_array / array_dim_count /
// array_first_dim / array_dims[] を保存し、psx_parse_member_array_suffixes_ex 後に
// 宣言子に追加 [N] が無ければ typedef の次元情報を取り込む。
#include <assert.h>

typedef int Row4[4];
typedef int Mat3x2[3][2];
typedef char Buf16[16];

struct R { Row4 r; int n; };
struct M { Mat3x2 m; };
struct B { Buf16 name; int age; };

int main(void) {
    /* (a) 1D 配列 typedef */
    struct R r1 = { {10, 20, 30, 40}, 100 };
    assert(r1.r[0] == 10);
    assert(r1.r[1] == 20);
    assert(r1.r[2] == 30);
    assert(r1.r[3] == 40);
    assert(r1.n == 100);

    /* 個別代入 */
    r1.r[2] = 99;
    assert(r1.r[2] == 99);

    /* sizeof は配列全体のバイト数 */
    assert(sizeof(r1.r) == sizeof(int) * 4);

    /* (b) 2D 配列 typedef */
    struct M m1 = { { {1, 2}, {3, 4}, {5, 6} } };
    assert(m1.m[0][0] == 1);
    assert(m1.m[0][1] == 2);
    assert(m1.m[1][0] == 3);
    assert(m1.m[2][1] == 6);

    /* (c) char 配列 typedef (文字列初期化) */
    struct B b1 = { "alice", 30 };
    assert(b1.name[0] == 'a');
    assert(b1.name[4] == 'e');
    assert(b1.name[5] == 0);  /* 残りはゼロ埋め */
    assert(b1.age == 30);

    return 0;
}
