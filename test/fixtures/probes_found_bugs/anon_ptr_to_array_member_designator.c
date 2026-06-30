// A promoted anonymous member can be an array of pointer-to-array typedefs.
// `.rows[i]` is still an array designator; the element type being a pointer
// must not make the parser reject the `[i]` part.
#include <assert.h>

typedef int (*RowPtr)[3];

struct H {
  struct {
    RowPtr rows[2];
  };
  int z;
};

int main(void) {
  int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
  int b[2][3] = {{7, 8, 9}, {10, 11, 12}};
  struct H h = {.rows = {a, b}, .z = 99};
  struct H k = {.rows[1] = b, .rows[0] = a, .z = 77};

  assert(h.rows[0][1][2] == 6 && h.rows[1][0][1] == 8 && h.z == 99);
  assert(k.rows[0][0][2] == 3 && k.rows[1][1][0] == 10 && k.z == 77);
  return 0;
}
