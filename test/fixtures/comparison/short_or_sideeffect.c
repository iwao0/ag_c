// 1 && bump() で 1 回呼ばれて g=1、bump 戻り値=1
// 0 || bump() で 1 回呼ばれて g=2、bump 戻り値=2
// 1 + 2*10 = 21? 実際は 1 && bump()→bump=1、 1+(0||2)*10 = 1+20=21
// テーブル期待値は 11、実態を見ると 1+10*1=11 (短絡で 1 回ずつ)
// 期待: exit=11
int g = 0;
int bump(void) { g = g + 1; return g; }
int main(void) {
    return (1 && bump()) + (0 || bump()) * 10;
}
