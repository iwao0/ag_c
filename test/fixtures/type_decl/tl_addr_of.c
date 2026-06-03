// _Thread_local のアドレス取得
// 期待: exit=5
_Thread_local int tv = 5;
int main(void) {
    int *p = &tv;
    return *p;
}
