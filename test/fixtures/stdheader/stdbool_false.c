// stdbool.h の false マクロ
#include <stdbool.h>
#include <assert.h>
int main(void) {
    bool b = false;
    assert(b == false);
    return 0;
}
