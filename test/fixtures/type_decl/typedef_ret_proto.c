// typedef long size_t; strlen プロトタイプ
// 期待: exit=5
#include <assert.h>
typedef long size_t;
size_t strlen(const char *s);
int main(void) { assert((int)strlen("hello") == 5); return 0; }
