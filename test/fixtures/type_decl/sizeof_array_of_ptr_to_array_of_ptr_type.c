// 冗長括弧パターンの sizeof = 16
// 期待: exit=16
int main(void) { return sizeof(int (*(*[2])[3])); }
