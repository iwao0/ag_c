// 同 enum 内で同名定数を重複定義は不正。
// 期待: ag_c は重複定義エラー
enum E { A = 1, A = 2 };
int main(void) { return A; }
