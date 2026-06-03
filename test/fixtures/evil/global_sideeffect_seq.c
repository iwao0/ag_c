// グローバル副作用の評価順序 (左から)
// a=1, b=2, c=3 → 100+20+3 = 123
// 期待: exit=123
int g_step = 0;
int next(void) { g_step = g_step + 1; return g_step; }
int main(void) {
    int a = next();
    int b = next();
    int c = next();
    return a * 100 + b * 10 + c;
}
