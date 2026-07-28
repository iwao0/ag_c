/*
 * C11 6.4.2.2 specifies __func__ as if it were a function-local
 * `static const char[]`.  Its address is therefore usable by static local
 * pointer and aggregate initializers, including address-constant offsets.
 */
#include <assert.h>
#include <string.h>

struct FunctionNameView {
  const char *begin;
  const char *suffix;
  const char (*whole)[13];
};

static int inspect_name(void) {
  static const char *direct = __func__;
  static const char *offset = __func__ + 8;
  static const char (*whole)[13] = &__func__;
  static const struct FunctionNameView view = {
      __func__, &__func__[8], &__func__};

  assert(direct == __func__);
  assert(offset == __func__ + 8);
  assert(whole == &__func__);
  assert(view.begin == direct);
  assert(view.suffix == offset);
  assert(view.whole == whole);
  assert(strcmp(direct, "inspect_name") == 0);
  assert(strcmp(offset, "name") == 0);
  assert((*whole)[12] == '\0');
  return 0;
}

int main(void) {
  assert(inspect_name() == 0);
  assert(inspect_name() == 0);
  return 0;
}
