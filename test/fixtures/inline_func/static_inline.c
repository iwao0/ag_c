// static inline 関数の呼び出し
// 期待: exit=42 (6*7)
static inline int mul(int a, int b) { return a * b; }
int main(void) {
    return mul(6, 7);
}
