#include <assert.h>
#include <string.h>

#define STRINGIZE_RAW(value) #value
#define STRINGIZE(value) STRINGIZE_RAW(value)
#define PASTE_RAW(left, right) left ## right
#define PASTE(left, right) PASTE_RAW(left, right)

#line 2147483647
_Static_assert(__LINE__ == 2147483647, "maximum __LINE__ value"); _Static_assert(_Generic(__LINE__, int: 1, default: 0), "maximum __LINE__ type"); _Static_assert(_Generic(PASTE(__LINE__, U), unsigned int: 1, default: 0), "maximum __LINE__ token paste"); int main(void) { assert(strcmp(STRINGIZE(__LINE__), "2147483647") == 0); return 0; }
