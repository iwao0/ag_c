// 多段の struct ポインタ仮引数 `struct N **root` で `(*root)->m` が E3005 で弾かれていた。
// register_param_lvar の struct ポインタ分岐 (param_ptr_levels >= 2) で pointer_qual_levels
// が立っていなかったため、build_unary_deref_node の `*root` で is_tag_pointer 伝播が
// pql>=2 を要求して 0 にクリアされ、`(*root)->m` の base_is_ptr=0 と判定されていた。
//
// 修正: param_ptr_levels >= 2 のとき var->pointer_qual_levels = param_ptr_levels を設定。
// ローカル `struct N **root` と同じノード属性になる。
#include <assert.h>

struct N { int v; struct N *left; struct N *right; };

void insert(struct N **root, int v) {
    if (*root == 0) return;  /* 簡略化: ルート無いなら何もしない */
    if (v < (*root)->v) (*root)->left = (struct N *)(long)v;
    else (*root)->right = (struct N *)(long)v;
}

int read_v(struct N **root) {
    return (*root)->v;
}

int main(void) {
    struct N r = {10, 0, 0};
    struct N *rp = &r;
    insert(&rp, 5);
    assert((long)r.left == 5);
    insert(&rp, 20);
    assert((long)r.right == 20);
    assert(read_v(&rp) == 10);
    return 0;
}
