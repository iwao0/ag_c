// ポインタ配列 (int* ptrs[3])
// 期待: exit=6 (1+2+3)
int main(void) {
    int a = 1;
    int b = 2;
    int c = 3;
    int *ptrs[3];
    ptrs[0] = &a;
    ptrs[1] = &b;
    ptrs[2] = &c;
    return *ptrs[0] + *ptrs[1] + *ptrs[2];
}
