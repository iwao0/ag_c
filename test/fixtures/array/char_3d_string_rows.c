#include <assert.h>

int main(void) {
    char rows[2][2][4] = {
        { "abc", "def" },
        { "ghi", "jkl" },
    };
    assert(sizeof(rows) == 16);
    assert(rows[0][0][0] == 'a');
    assert(rows[0][1][2] == 'f');
    assert(rows[1][0][1] == 'h');
    assert(rows[1][1][2] == 'l');
    assert(rows[1][1][3] == 0);
    return 0;
}
