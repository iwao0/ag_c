// string.h strerror minimal runtime string
// Expected: exit=0
#include <assert.h>
#include <string.h>

int main(void) {
    char *msg = strerror(5);
    assert(msg != 0);
    assert(strlen(msg) > 0);
    return 0;
}
