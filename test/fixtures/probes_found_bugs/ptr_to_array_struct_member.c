// struct S (*ap)[N]; (*ap)[i].m — tag carry through deref of pointer-to-array
// build_unary_deref_node の is_tag_ptr ガードで tag が落ち、(*ap)[i].m の `.`
// 解決が E3005 で弾かれていた回帰。
#include <assert.h>

struct S { int a; int b; };
union U { int n; struct S s; };

struct S g2[2][3] = {{{1,2},{3,4},{5,6}},{{7,8},{9,10},{11,12}}};

static struct S gd[3] = {{1,2},{3,4},{5,6}};
struct S (*gap)[3] = &gd;

void set(struct S (*ap)[3], int i, int va, int vb) {
    (*ap)[i].a = va;
    (*ap)[i].b = vb;
}

int sum_param(struct S (*ap)[3]) {
    int s = 0;
    for (int i = 0; i < 3; i++) s += (*ap)[i].a + (*ap)[i].b;
    return s;
}

int main(void) {
    /* 局所: 直接メンバ読み出し */
    struct S arr[3] = {{1,2},{3,4},{5,6}};
    struct S (*ap)[3] = &arr;
    assert((*ap)[0].a == 1);
    assert((*ap)[1].b == 4);
    assert((*ap)[2].a == 5);

    /* 局所: struct 値コピー (修正前から動いていた経路) */
    struct S s0 = (*ap)[1];
    assert(s0.a == 3 && s0.b == 4);

    /* 局所: メンバ書き込み */
    (*ap)[0].a = 100;
    (*ap)[2].b = 200;
    assert(arr[0].a == 100);
    assert(arr[2].b == 200);

    /* 仮引数経由 */
    set(ap, 1, 700, 800);
    assert(arr[1].a == 700 && arr[1].b == 800);
    /* set 後の合計: 100+2+700+800+5+200 = 1807 */
    assert(sum_param(ap) == 1807);

    /* 2D ポインタ-to-配列 */
    struct S (*ap2)[2][3] = &g2;
    assert((*ap2)[0][0].a == 1);
    assert((*ap2)[1][2].b == 12);
    (*ap2)[1][0].a = 999;
    assert(g2[1][0].a == 999);

    /* union 要素のポインタ-to-配列 */
    union U arr3[2] = { {.n = 5}, {.s = {3, 4}} };
    union U (*up)[2] = &arr3;
    assert((*up)[0].n == 5);
    assert((*up)[1].s.a == 3 && (*up)[1].s.b == 4);

    /* グローバル ポインタ-to-配列 */
    int gs = 0;
    for (int i = 0; i < 3; i++) gs += (*gap)[i].a + (*gap)[i].b;
    assert(gs == 21);
    (*gap)[0].a = 50;
    assert(gd[0].a == 50);

    return 0;
}
