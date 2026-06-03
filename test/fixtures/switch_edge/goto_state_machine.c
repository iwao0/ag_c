// goto ベースのステートマシン (st 0→1→2)
// r = 1 + 10 + 100 = 111
// 期待: exit=111
int main(void) {
    int st = 0;
    int r = 0;
again:
    switch (st) {
        case 0: r = r + 1; st = 1; goto again;
        case 1: r = r + 10; st = 2; goto again;
        case 2: r = r + 100; break;
    }
    return r;
}
