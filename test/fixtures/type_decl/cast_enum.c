// (enum E)42 → 42 (受理確認)
// 期待: exit=42
int main(void) {
    enum E { A = 1 };
    return (enum E)42;
}
