// _Thread_local 初期化
// 期待: exit=7
_Thread_local int tl_val = 7;
int main(void) { return tl_val; }
