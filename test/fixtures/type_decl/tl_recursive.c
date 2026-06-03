// _Thread_local を共有して再帰で累算
// 期待: exit=15 (5+4+3+2+1)
_Thread_local int tx = 0;
int tf(int n) {
    if (n <= 0) return tx;
    tx = tx + n;
    return tf(n - 1);
}
int main(void) { return tf(5); }
