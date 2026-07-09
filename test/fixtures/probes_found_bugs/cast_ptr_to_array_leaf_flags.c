// Pointer-to-array cast keeps leaf pointee flags in both typed metadata and
// legacy node metadata. Without this, `unsigned char (*)[N]` loaded through a
// cast can sign-extend, and `_Bool (*)[N]` assignment can skip 0/1 normalization.
#include <assert.h>

int main(void) {
  unsigned char u[1][3] = {{1, 2, 255}};
  void *uv = u;
  unsigned char (*up)[3] = (unsigned char (*)[3])uv;
  assert(up[0][0] == 1);
  assert(up[0][2] == 255);

  _Bool b[1][2] = {{0, 1}};
  void *bv = b;
  _Bool (*bp)[2] = (_Bool (*)[2])bv;
  bp[0][0] = 99;
  assert(bp[0][0] == 1);
  assert(bp[0][1] == 1);

  return 0;
}
