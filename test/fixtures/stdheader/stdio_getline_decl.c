// stdio.h getline declaration
// 期待: exit=0
#include <stddef.h>
#include <stdio.h>

int main(void) {
    return sizeof(&getline) == sizeof(void *) ? 0 : 1;
}
