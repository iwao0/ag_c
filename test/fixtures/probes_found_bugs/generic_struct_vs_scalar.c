// _Generic で、制御式が int・関連型が「単一 int メンバの struct」のとき、
// 型一致判定が control 側しか tag を見ず、下のスカラ経路でサイズ (4) が一致して
// struct 関連に誤マッチしていた (`_Generic((int), Anon:.., int:..)` が Anon を選ぶ)。
// どちらか一方でも tag を持てば tag 一致を要求するよう修正。
#include <assert.h>

typedef struct { int a; } Anon;
typedef union { int u; } AnonU;
struct Tagged { int x; };

int main(void) {
    int i = 0;
    /* int 制御は単一 int メンバ struct/union 関連に一致しない */
    assert(_Generic((i), Anon: 2, int: 3, default: 0) == 3);
    assert(_Generic((i), AnonU: 2, int: 3, default: 0) == 3);

    /* struct 制御は同じ struct にのみ一致 */
    Anon an = { 1 };
    assert(_Generic((an), Anon: 4, int: 1, default: 0) == 4);
    struct Tagged t = { 1 };
    assert(_Generic((t), struct Tagged: 7, int: 1, default: 0) == 7);

    /* 異なる tag 同士は一致しない (default) */
    assert(_Generic((an), struct Tagged: 1, default: 9) == 9);

    /* スカラ同士の判定は不変 */
    long l = 0;
    assert(_Generic((l), int: 1, long: 5, default: 0) == 5);
    return 0;
}
