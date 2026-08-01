/* An explicit NUL remains content in a selected UTF-16 union member. */
#include <uchar.h>

union value {
  unsigned long long raw;
  char16_t text[2];
};

static union value instance = {
    .text = u"\U0001F600\0",
};

int main(void) {
  return 0;
}
