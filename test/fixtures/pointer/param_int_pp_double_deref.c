// 多段ポインタ仮引数 int **pp の **pp 操作
// 注: int *p 仮引数の修正で elem_size を pointee サイズへ揃える際、
//     多段ポインタ (**pp) では pointee がポインタなので elem_size=8 を維持する必要がある。
//     先行の素朴な修正では elem_size=4 になり、本ケースが SEGV を起こした。
// 期待: exit=12 (10 を 2 回インクリメント)
void inc(int **pp) { (**pp)++; }
int main(void) {
    int x = 10;
    int *p = &x;
    inc(&p);
    inc(&p);
    return x;
}
