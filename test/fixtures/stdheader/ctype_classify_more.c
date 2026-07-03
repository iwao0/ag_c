// ctype.h classification and case conversion
// Expected: exit=0
#include <assert.h>
#include <ctype.h>

int main(void) {
    assert(isalnum('A') != 0);
    assert(isalnum('7') != 0);
    assert(isalnum('@') == 0);
    assert(isblank(' ') != 0);
    assert(isblank('\t') != 0);
    assert(isblank('\n') == 0);
    assert(iscntrl('\n') != 0);
    assert(iscntrl(127) != 0);
    assert(iscntrl('A') == 0);
    assert(isgraph('!') != 0);
    assert(isgraph(' ') == 0);
    assert(islower('z') != 0);
    assert(islower('Z') == 0);
    assert(isprint(' ') != 0);
    assert(isprint('\n') == 0);
    assert(ispunct('!') != 0);
    assert(ispunct('A') == 0);
    assert(isspace(' ') != 0);
    assert(isspace('\n') != 0);
    assert(isspace('x') == 0);
    assert(isupper('Z') != 0);
    assert(isupper('z') == 0);
    assert(isxdigit('f') != 0);
    assert(isxdigit('G') == 0);
    assert(tolower('A') == 'a');
    assert(tolower('!') == '!');
    assert(toupper('a') == 'A');
    assert(toupper('!') == '!');
    return 0;
}
