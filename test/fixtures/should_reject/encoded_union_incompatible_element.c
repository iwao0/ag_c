/* A union member of signed int is incompatible with UTF-32 string code units. */
#include <uchar.h>

union value {
  unsigned long long raw;
  int text[2];
};

static union value instance = {.text = U"x"};

int main(void) {
  return 0;
}
