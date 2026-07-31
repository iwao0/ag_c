#include <stddef.h>

struct value {
  unsigned member : 1;
};

// A bit-field has no addressable byte offset for offsetof.
int main(void) {
  return (int)offsetof(struct value, member);
}
