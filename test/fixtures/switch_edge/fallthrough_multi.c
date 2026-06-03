// 複数 case のフォールスルー (case 3 →4 →5 で r=3+40-(255)? 計算: r=3, +40=43, +500=543, mod 256=31)
// 期待: exit=31
int main(void) {
    int r = 0;
    int x = 3;
    switch (x) {
        case 1: r = 1; break;
        case 2: r = 2; break;
        case 3: r = 3;
        case 4: r = r + 40;
        case 5: r = r + 500; break;
        default: r = 9;
    }
    return r;
}
