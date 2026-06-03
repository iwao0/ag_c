// 配列ポインタ配列 [2] の sizeof = 16
// 期待: exit=16
int main(void) { return sizeof(int (*[2])[3]); }
