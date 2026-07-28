/*
 * strchr/strrchr compare after converting the int search value to char.
 * Expected: exit=0
 */
#include <string.h>

static char *(*strchr_signature)(const char *, int) = strchr;
static char *(*strrchr_signature)(const char *, int) = strrchr;

int main(void) {
    char high = (char)0xff;
    char text[5] = {high, 'A', high, 'B', '\0'};
    int promoted_high = high;

    if (strchr_signature(text, promoted_high) != text) return 1;
    if (strrchr_signature(text, promoted_high) != text + 2) return 2;
    if (strchr_signature(text, '\0') != text + 4) return 3;
    if (strrchr_signature(text, '\0') != text + 4) return 4;
    if (strchr_signature(text, 'Z') != 0) return 5;
    if (strrchr_signature(text, 'Z') != 0) return 6;
    return 0;
}
