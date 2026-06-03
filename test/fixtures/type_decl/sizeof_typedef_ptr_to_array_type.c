// sizeof(typedef 配列型へのポインタ) = 8
// 期待: exit=8
typedef int A3[3];
int main(void) { return sizeof(A3 (*)[2]); }
