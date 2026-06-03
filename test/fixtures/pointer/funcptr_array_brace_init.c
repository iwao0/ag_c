// 関数ポインタ配列の波括弧初期化 int (*ops[N])(...) = { f1, f2 };
// 修正前: E3064 "スカラ初期化子の波括弧内は1要素のみ対応" でコンパイル失敗
//        (配列ではなく単一ポインタとして登録されていた)
// 期待: exit=20 ((10+3) + (10-3))
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int main(void) {
    int (*ops[2])(int, int) = { add, sub };
    return ops[0](10, 3) + ops[1](10, 3);
}
