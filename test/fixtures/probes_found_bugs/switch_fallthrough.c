// 続き47: switch fallthrough warning (W3017 / clang -Wimplicit-fallthrough 相当)。
//
// parse_stmt_block で「直前の文が break/return/continue/goto で終わっていないまま
// 次の case/default に到達する」場合に W3017 を出す。stmt_tail_terminates() で
// case 本体が block の場合や、case 自身が更にネストした case の場合も追跡する。
//
// 本 fixture は false-positive を出さないことの確認 (合法形のみ)。
#include <assert.h>

int classify_break(int x) {
    int r = 0;
    switch (x) {
        case 0: r = 100; break;
        case 1: r = 200; break;
        default: r = 300;
    }
    return r;
}

int classify_return(int x) {
    switch (x) {
        case 0: return 10;
        case 1: return 20;
        default: return 30;
    }
}

/* 連続する case ラベル (`case 0: case 1: ...`) は内部で再帰ネストされ、
 * 外側ブロックには case が連続して現れないので fallthrough 警告にならない。 */
int classify_combined(int x) {
    int r = 0;
    switch (x) {
        case 0:
        case 1:
        case 2: r = 1; break;
        case 3:
        case 4: r = 2; break;
        default: r = 0;
    }
    return r;
}

/* case 本体を { } で囲った場合: case rhs が ND_BLOCK で、その block の末尾が
 * break なら stmt_tail_terminates が真を返す。fallthrough 警告は出ない。 */
int classify_braced(int x) {
    int r = 0;
    switch (x) {
        case 0: { r = 10; break; }
        case 1: { r = 20; break; }
        default: { r = 30; }
    }
    return r;
}

/* switch の前に decl を置く構成。最初の case 以前は seen_case_in_block=0 なので
 * fallthrough 検出が走らない。 */
int classify_with_setup(int x) {
    int tmp;
    switch (x) {
        case 0: tmp = 100; return tmp;
        case 1: tmp = 200; return tmp;
        default: return 0;
    }
}

int main(void) {
    assert(classify_break(0) == 100);
    assert(classify_break(1) == 200);
    assert(classify_break(99) == 300);
    assert(classify_return(0) == 10);
    assert(classify_return(1) == 20);
    assert(classify_return(99) == 30);
    assert(classify_combined(0) == 1);
    assert(classify_combined(2) == 1);
    assert(classify_combined(3) == 2);
    assert(classify_combined(99) == 0);
    assert(classify_braced(0) == 10);
    assert(classify_braced(1) == 20);
    assert(classify_with_setup(0) == 100);
    assert(classify_with_setup(99) == 0);
    return 0;
}
