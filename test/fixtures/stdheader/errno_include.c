// errno.h の EDOM == 33
// 期待: exit=42
#include <errno.h>
int main(void) {
    return EDOM == 33 ? 42 : 0;
}
