// 論理 OR よりも AND が優先 (1 || (0 && 0)) = 1
// 期待: exit=1
#include <assert.h>
int main() { assert(1||0&&0); return 0; }
