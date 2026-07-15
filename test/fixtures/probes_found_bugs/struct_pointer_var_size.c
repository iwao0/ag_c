// struct ポインタ型のローカル変数 (`struct N *p`) のノード type_size が、
// ポインタサイズ (8) ではなく pointee の struct サイズ (例 16) になっていたバグ。
// build_lvar_or_vla_node が type_size を `is_array / size>elem_size` だけで決め、
// is_tag_pointer / pointer_qual_levels を見ていなかった。struct ポインタは
// size==8 < elem_size(=16) なので elem_size(16) に落ちていた。
// 影響:
//   (1) sizeof(struct N*) が 16 を返す。
//   (2) `p = p->next` 等のポインタ代入が type_size>8 で「struct コピー (memcpy)」
//       経路に入り、16 バイトをコピー。ポインタ値 (先頭 8B) は正しく入るが、
//       余分な 8B が隣接スタック変数を破壊する。連結リスト走査で次ノードが
//       書き潰され、無限ループや誤った値・SIGSEGV になっていた。
// 修正: lvar_is_pointer (is_tag_pointer / pointer_qual_levels / pointee_fp_kind
//       を含む) を先に求め、ポインタなら type_size=8 とする。
// 修正前: 連結リストの合計が壊れる / sizeof 誤り
// 期待: exit=42
#include <assert.h>
struct N { int v; struct N *next; };

int main(void) {
    // ランタイム代入で next を繋ぐ (brace 初期化でなくても壊れていた)
    struct N a, b, c;
    a.v = 5;  b.v = 15;  c.v = 22;
    a.next = &b;  b.next = &c;  c.next = 0;

    // self-assign `p = p->next` での走査 (16B コピーが c を書き潰していた)
    int sum = 0;
    struct N *p = &a;
    while (p) {
        sum += p->v;        // 5 + 15 + 22 = 42
        p = p->next;
    }

    // sizeof が target のポインタサイズを返すことも確認
    if (sizeof(struct N *) != sizeof(void*)) return 1;
    if (sizeof(p) != sizeof(void*)) return 2;

    assert(sum == 42); return 0;             // 42
}
