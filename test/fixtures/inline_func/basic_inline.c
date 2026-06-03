// inline 指定子 (C11 6.7.4): 単一翻訳単位では通常関数と同等に動作
// 期待: exit=42
inline int add(int a, int b) { return a + b; }
int main(void) {
    return add(20, 22);
}
