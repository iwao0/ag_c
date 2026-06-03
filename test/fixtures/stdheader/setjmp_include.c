// setjmp.h のインクルードが通ること
// 期待: exit=42
#include <setjmp.h>
int main(void) { return 42; }
