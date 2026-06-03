// ローカルスコープでの `extern int g;` 宣言が同名グローバルを参照すること
// 期待: exit=42
int g = 42;
int main(void) {
    extern int g;
    return g;
}
