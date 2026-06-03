// 関数ポインタ配列 [3] の sizeof = 24
// 期待: exit=24
int main(void) { return sizeof(int (*[3])(int)); }
