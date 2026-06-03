// goto ループ + switch でステートマシン (1+10+100 = 111)
// 期待: exit=111
int main(void) {
    int i = 0;
    int s = 0;
loop:
    switch (i) {
        case 0: s = s + 1; break;
        case 1: s = s + 10; break;
        case 2: s = s + 100; break;
        default: goto end;
    }
    i = i + 1;
    goto loop;
end:
    return s;
}
