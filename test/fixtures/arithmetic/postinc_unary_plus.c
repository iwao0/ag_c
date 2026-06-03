// 後置インクリメントと単項 + を併用
// x++ 評価値 1、 +1 → 1+1=2
// 期待: exit=2
int main(void) { int x=1; return x++ + +1; }
