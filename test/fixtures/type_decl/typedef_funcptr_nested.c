// 関数ポインタ typedef (冗長括弧)
// 期待: exit=0
typedef int (((*fp_t)))(int);
int main(void) {
    fp_t p;
    return 0;
}
