/* A one-element member cannot hold a UTF-16 surrogate pair. */
#include <uchar.h>

struct value {
  char16_t text[1];
};

struct value instance = {u"\U0001F600"};

int main(void) {
  return 0;
}
