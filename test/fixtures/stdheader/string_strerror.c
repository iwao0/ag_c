// string.h strerror minimal runtime string
// Expected: exit=0
#include <assert.h>
#include <string.h>

int main(void) {
    char *ok = strerror(0);
    char *msg = strerror(5);
    assert(ok != 0);
    assert(msg != 0);
    assert(strlen(ok) > 0);
    assert(strlen(msg) > 0);
    assert(strcmp(ok, msg) != 0);
    return 0;
}
