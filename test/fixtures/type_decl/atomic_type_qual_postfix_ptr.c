// int _Atomic * (atomic int へのポインタ)
// 期待: exit=7
int main(void) {
    int x = 7;
    int _Atomic *p = &x;
    return *p;
}
