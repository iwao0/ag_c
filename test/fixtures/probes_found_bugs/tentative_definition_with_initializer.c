#include <assert.h>

int total;
extern int total;
int total = 7;
extern int total;

static int local_total;
static int local_total = 5;

int main(void) {
  assert(total == 7);
  assert(local_total == 5);
  total += local_total;
  assert(total == 12);
  return 0;
}
