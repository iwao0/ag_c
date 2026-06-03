// 関数を跨いだ _Thread_local 更新
// 期待: exit=3
_Thread_local int tg = 0;
void tinc(void) { tg = tg + 1; }
int main(void) {
    tinc(); tinc(); tinc();
    return tg;
}
