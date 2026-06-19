// char** から **pp で 'A' (=65) を得る
// 期待: exit=65
#include <assert.h>
int main(void) {
    char c = 'A';
    char *p = &c;
    char **pp = &p;
    assert(**pp == 'A');
    return 0;
}
