// _Thread_local の未初期化はゼロ
// 期待: exit=0
_Thread_local int tu;
int main(void) { return tu; }
