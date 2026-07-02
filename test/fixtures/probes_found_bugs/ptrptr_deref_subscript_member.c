// `T **pp; (*pp)[i].member` が E3005 で弾かれていた。
// `*pp` の結果は単段の `T *` なので、subscript 後は `T` 実体になる必要がある。
// build_subscript_deref が `(*pp)[i]` をまだ tag pointer として扱い、
// 続く `.member` を「ポインタへの .」と誤判定していた。
#include <assert.h>

typedef struct TypeInfo {
    int raw_len;
    int raw;
} type_t;

int write_member(type_t **types, int i) {
    if ((*types)[i].raw_len != i + 3) return 1;
    (*types)[i].raw = (*types)[i].raw_len + 1;
    return (*types)[i].raw;
}

int main(void) {
    type_t items[2] = {{3, 0}, {4, 0}};
    type_t *p = items;
    assert(write_member(&p, 1) == 5);
    assert(items[1].raw == 5);
    return 0;
}
