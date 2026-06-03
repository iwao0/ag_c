// (short)(700*100) = 70000 → short ラップ = 4464 → exit code mod 256 = 112
// 期待: exit=112
int main(void) { return (short)(700 * 100); }
