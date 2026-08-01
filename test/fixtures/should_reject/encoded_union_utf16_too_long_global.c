/* A union's selected UTF-16 member must fit the full surrogate pair. */
#include <uchar.h>

union value {
  unsigned long long raw;
  char16_t text[1];
};

union value instance = {.text = u"\U0001F600"};

int main(void) {
  return 0;
}
