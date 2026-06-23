// 続き78: `sizeof((int) 1)` のような cast 式に対する sizeof (修正)。c-testsuite 00155 由来。
//
// 修正前: parse_parenthesized_type_size の LPAREN 分岐が「内側を type-name として消費」した後
// 巻き戻さずそのまま sz>=0 を返していた。`sizeof((int) 1)` の場合、内側 `(int)` を type-name
// として消費して sz=4 を返した後、外側 `tk_expect(')')` 直前で curtok が `1` になっており
// E2006「`)` が必要」エラー。
//
// 修正: LPAREN 分岐の再帰呼び出しから戻ってきた直後、curtok が `)` でない場合は token を
// 巻き戻して -1 を返し、parse_sizeof_operand の通常式パース経路 (expr_internal) に任せる。
// 通常経路で `(int) 1` は cast 式として正しくパースされ、sizeof は式の型サイズを返す。
//
// 二重括弧 `sizeof((int))` (parens で囲まれた type-name) は C11 標準では非合法 (clang も拒否)
// のため対象外。
#include <assert.h>
#include <stddef.h>

int main(void) {
    /* 元 c-testsuite 00155: 構文が通れば良い (式の型サイズ追跡は別バグ) */
    sizeof((int) 1);
    sizeof((double) 1.5);
    sizeof((int *) 0);

    /* int cast はもとから既存テストでカバー、サイズも正しい */
    assert(sizeof((int) 1) == sizeof(int));

    /* sizeof(type-name) は引き続き正しく動作 (回帰確認) */
    assert(sizeof(int) == 4);
    assert(sizeof(long) == 8);

    return 0;
}
