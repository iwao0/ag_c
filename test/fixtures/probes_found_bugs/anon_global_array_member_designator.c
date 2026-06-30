// Global initializer for an anonymous struct member must not count both the
// unnamed aggregate storage and its promoted members as separate flat slots.
// Before the fix, `.a = {1, 2}` started after the unnamed inner struct and
// emitted two leading zeros for `h`.
#include <assert.h>

struct H {
  struct {
    int a[2];
  };
  int z;
};

struct H h = {.a = {1, 2}, .z = 3};
struct H k = {.a[1] = 7, .a[0] = 5, .z = 11};
struct H p = {{13, 17}, 19};

int main(void) {
  assert(h.a[0] == 1 && h.a[1] == 2 && h.z == 3);
  assert(k.a[0] == 5 && k.a[1] == 7 && k.z == 11);
  assert(p.a[0] == 13 && p.a[1] == 17 && p.z == 19);
  return 0;
}
