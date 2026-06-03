// sizeof(typedef A3[2]) = 24 (= 3*2*4)
// 期待: exit=24
typedef int A3[3];
int main(void) { return sizeof(A3[2]); }
