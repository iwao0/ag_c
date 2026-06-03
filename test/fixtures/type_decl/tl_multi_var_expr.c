// 複数 _Thread_local 変数
// 期待: exit=35 (2*10+3*5)
_Thread_local int ta = 2;
_Thread_local int tb = 3;
int main(void) { return ta * 10 + tb * 5; }
