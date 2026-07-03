// string.h strtok tokenization
// Expected: exit=0
#include <assert.h>
#include <string.h>

int main(void) {
    char text[] = "a,bb;ccc";
    char *tok = strtok(text, ",;");
    assert(tok != 0);
    assert(strcmp(tok, "a") == 0);
    tok = strtok(0, ",;");
    assert(tok != 0);
    assert(strcmp(tok, "bb") == 0);
    tok = strtok(0, ",;");
    assert(tok != 0);
    assert(strcmp(tok, "ccc") == 0);
    assert(strtok(0, ",;") == 0);

    char leading[] = ",;x,,y";
    tok = strtok(leading, ",;");
    assert(tok != 0);
    assert(strcmp(tok, "x") == 0);
    tok = strtok(0, ",;");
    assert(tok != 0);
    assert(strcmp(tok, "y") == 0);
    assert(strtok(0, ",;") == 0);
    return 0;
}
