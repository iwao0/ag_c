// Integer constant to pointer casts should stay as ND_CAST wrappers, not as
// specially flagged ND_NUM nodes. Global/static init and comparisons must still
// understand the casted constant.
#include <assert.h>

static int *gp = (int *)0x1000;

int main(void) {
  int *lp = (int *)0x1000;
  assert(gp == lp);
  assert(gp == (int *)0x1000);
  return 0;
}
