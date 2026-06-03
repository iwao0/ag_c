// *&*&*&x のチェーン
// 期待: exit=42
int main(void) {
    int x = 42;
    int *p = &x;
    return *&*&*&x;
}
