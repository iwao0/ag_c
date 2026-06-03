// 後置デクリメントと単項 - を併用
// x-- 評価値 5、 - -3 → 5+3=8
// 期待: exit=8
int main(void) { int x=5; return x-- - -3; }
