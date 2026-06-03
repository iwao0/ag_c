// `x+++y+++z` は `(x++) + (y++) + z` と読まれる
// x++=2, y++=3, z=4 → 2+3+4=9
// 期待: exit=9
int main(void) { int x=2; int y=3; int z=4; return x+++y+++z; }
