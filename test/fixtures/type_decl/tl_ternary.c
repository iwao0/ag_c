// _Thread_local + 三項演算子
// 期待: exit=9 (3*3)
_Thread_local int tt = 3;
int main(void) {
    return tt > 2 ? tt * tt : 0;
}
