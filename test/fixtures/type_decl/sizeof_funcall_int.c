// sizeof(funcall()) は戻り値の型サイズを返す (副作用は評価しない)
// int 戻り値関数 → 4
// 期待: exit=4
int identity(int x) { return x; }
int main(void) {
    return (int)sizeof(identity(42));
}
