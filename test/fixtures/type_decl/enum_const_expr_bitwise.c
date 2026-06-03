// enum 定数でビット演算
// A=1, B=~1=-2, C=(1<<3)|2=10, D=(10&10)^1=11
// 期待: exit=11
int main(void) {
    enum E { A = 1, B = ~A, C = (A << 3) | 2, D = (C & 10) ^ 1 };
    return D;
}
