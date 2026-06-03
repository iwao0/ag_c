// _Thread_local を switch 条件に
// 期待: exit=20
_Thread_local int tsw = 2;
int main(void) {
    switch (tsw) {
        case 1: return 10;
        case 2: return 20;
        default: return 99;
    }
}
