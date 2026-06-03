// typedef int (*(*arr_t)[2])(int): 関数ポインタ配列へのポインタ
// 期待: exit=0
typedef int (*(*arr_t)[2])(int);
int main(void) {
    arr_t p;
    return 0;
}
