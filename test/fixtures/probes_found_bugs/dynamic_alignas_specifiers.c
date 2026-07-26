#include <assert.h>
#include <stdint.h>

#define ALIGN_EXPR_1 _Alignas(16)
#define ALIGN_EXPR_2 ALIGN_EXPR_1 ALIGN_EXPR_1
#define ALIGN_EXPR_4 ALIGN_EXPR_2 ALIGN_EXPR_2
#define ALIGN_EXPR_8 ALIGN_EXPR_4 ALIGN_EXPR_4
#define ALIGN_EXPR_16 ALIGN_EXPR_8 ALIGN_EXPR_8

#define ALIGN_TYPE_1 _Alignas(long long)
#define ALIGN_TYPE_2 ALIGN_TYPE_1 ALIGN_TYPE_1
#define ALIGN_TYPE_4 ALIGN_TYPE_2 ALIGN_TYPE_2
#define ALIGN_TYPE_8 ALIGN_TYPE_4 ALIGN_TYPE_4
#define ALIGN_TYPE_16 ALIGN_TYPE_8 ALIGN_TYPE_8

ALIGN_EXPR_16 ALIGN_EXPR_1 static int global_value = 5;

static int local_value(void) {
  ALIGN_TYPE_16 ALIGN_TYPE_1 long long value = 7;
  assert((uintptr_t)&value % _Alignof(long long) == 0);
  return (int)value;
}

int main(void) {
  assert((uintptr_t)&global_value % 16 == 0);
  assert(global_value == 5);
  assert(local_value() == 7);
  return 0;
}
