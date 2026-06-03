// extern inline 関数の呼び出し
// 期待: exit=42 (50-8)
extern inline int sub(int a, int b) { return a - b; }
int main(void) {
    return sub(50, 8);
}
