#include <stddef.h>

// The first offsetof argument must name a complete structure or union type.
int main(void) {
  return (int)offsetof(int, member);
}
