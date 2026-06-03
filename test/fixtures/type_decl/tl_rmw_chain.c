// _Thread_local read-modify-write の連鎖
// 期待: exit=5
_Thread_local int tw = 1;
int main(void) {
    tw = tw + 1;
    tw = tw + 1;
    tw = tw + 1;
    tw = tw + 1;
    return tw;
}
