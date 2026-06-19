// struct を返す関数の戻り値を別の関数の値渡し引数にする
// 期待: exit=42
#include <assert.h>
struct Val { int v; };
struct Val make_val(int n) {
    struct Val v = {n};
    return v;
}
int get_v(struct Val p) { return p.v; }
int main(void) {
    assert(get_v(make_val(42)) == 42);
    return 0;
}
