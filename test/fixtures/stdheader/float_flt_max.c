// float.h の FLT_MAX
// 期待: exit=42
#include <float.h>
int main(void) {
    float f = FLT_MAX;
    return f > 1.0F ? 42 : 0;
}
