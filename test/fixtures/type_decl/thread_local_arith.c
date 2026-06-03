// _Thread_local 演算
// 期待: exit=15
_Thread_local int tl_a = 10;
int main(void) {
    tl_a = tl_a + 5;
    return tl_a;
}
