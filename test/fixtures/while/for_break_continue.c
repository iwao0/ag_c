// for ループ内で break と continue を併用
// i=0,1,3,4 を s に加算 (i==2 で skip、i==5 で break) → 0+1+3+4 = 8
// 期待: exit=8
int main(void) {
    int i;
    int s = 0;
    for (i = 0; i < 6; i = i + 1) {
        if (i == 2) continue;
        if (i == 5) break;
        s = s + i;
    }
    return s;
}
