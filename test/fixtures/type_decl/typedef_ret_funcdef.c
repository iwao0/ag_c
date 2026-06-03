// typedef で long を別名化し、戻り値型に使う
// 期待: exit=7
typedef long mylong;
mylong add(mylong a, mylong b) { return a + b; }
int main(void) { return (int)add(3, 4); }
