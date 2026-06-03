// 冗長な括弧付きの関数名定義 `int (f)(int x)`
// 期待: exit=42
int (f)(int x) { return x; }
int main(void) {
    return f(42);
}
