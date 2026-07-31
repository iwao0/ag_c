#include <stddef.h>

struct value {
  int member;
};

// The offsetof designator must name an existing member.
int main(void) {
  return (int)offsetof(struct value, missing);
}
