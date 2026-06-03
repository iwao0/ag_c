// && 短絡 + 副作用: inc は呼ばれない
// 0 && inc() → 0 (inc 未呼出)
// 1 || inc() → 1 (inc 未呼出)
// g=0、 (0&&...)*1000 + (1||...)*100 + 0 = 100
// 期待: exit=100
int g = 0;
int inc(void) { g = g + 1; return g; }
int main(void) {
    return (0 && inc()) * 1000 + (1 || inc()) * 100 + g;
}
