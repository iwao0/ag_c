// (int const const) で重複修飾子をキャスト
// 期待: exit=21
#include <assert.h>
int main(void) { assert((int const const)21 == 21); return 0; }
