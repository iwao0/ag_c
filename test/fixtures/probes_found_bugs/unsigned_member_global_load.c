// unsigned のグローバル変数 / struct・union メンバを直接比較すると、load が
// 符号拡張 (ldrsw) され値が化けていたバグ。
// `unsigned g=0xFFFFFFFF; g==0xFFFFFFFFu` が、g を sign-extend (= -1, 0xFFFF...FFFF)
// でロードし、定数 0xFFFFFFFF (zero-extend) と一致せず false になっていた。
// ローカル unsigned 変数は is_unsigned が伝播していて zero-extend されるため動いて
// いたが、global_var_t / tag_member は is_unsigned を持たず伝播していなかった。
// 修正: global_var_t と tag_member に is_unsigned を追加し、ノード (ND_GVAR /
//   メンバ ND_DEREF) へ伝播して load を zero-extend にする。
// 修正前: 直接比較が false に化ける
// 期待: exit=42
unsigned gu = 0xFFFFFFFFu;
struct S { unsigned u; int s; };
union  U { int i; unsigned u; };
struct S gs = {0xFFFFFFFFu, -1};

int main(void) {
    // (1) unsigned グローバル
    if (!(gu == 0xFFFFFFFFu)) return 1;
    if (gu <= 0x7FFFFFFFu) return 2;          // unsigned: 0xFFFFFFFF > 0x7FFFFFFF

    // (2) unsigned struct メンバ (ローカル)
    struct S x; x.u = 0xFFFFFFFFu;
    if (!(x.u == 0xFFFFFFFFu)) return 3;

    // (3) unsigned struct メンバ (グローバル)
    if (!(gs.u == 0xFFFFFFFFu)) return 4;

    // (4) union 型パンニング: int=-1 を unsigned で読む
    union U y; y.i = -1;
    if (!(y.u == 0xFFFFFFFFu)) return 5;

    // (5) unsigned メンバの除算は符号なし
    struct S z; z.u = 10u; int neg = -3;
    if ((int)(z.u / 5u) != 2) return 6;

    return 42;
}
