// _Atomic for ループ
// 期待: exit=10
_Atomic int al = 0;
int main(void) {
    int i = 0;
    for (i = 0; i < 10; i = i + 1) { al = al + 1; }
    return al;
}
