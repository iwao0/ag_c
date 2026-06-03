// 関数ポインタ配列のサイズ推定 int (*ops[])(...) = { ... };
// 修正前: 内側 [] が空のとき inner_array_mul=0 になり、配列ではなく
//        単一ポインタとして登録され、初期化子で "スカラ初期化子" エラー
// 期待: exit=32 ((2+3) + (10-4) + (3*7))
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }
int main(void) {
    int (*ops[])(int, int) = { add, sub, mul };
    return ops[0](2, 3) + ops[1](10, 4) + ops[2](3, 7);
}
