// float.h の FLT_RADIX は 2 (IEEE754 binary)
// 期待: exit=42
#include <float.h>
int main(void) {
    return FLT_RADIX == 2 ? 42 : 0;
}
