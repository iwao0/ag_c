// C11 defines an integer constant expression with value zero, and that
// expression cast to void *, as null pointer constants. The cast form can
// initialize, assign, compare, pass, return, or conditionally combine with
// any object or function pointer type.
#include <stddef.h>

typedef int callback_type(int);

static int add_one(int value) {
  return value + 1;
}

static callback_type *global_callback = (void *)0;
static callback_type *global_generic_callback =
    _Generic(0, int: (void *)0, default: add_one);

static int accepts_callback(callback_type *callback) {
  return callback == NULL;
}

static callback_type *returns_callback(void) {
  return NULL;
}

int main(void) {
  if (global_callback != NULL ||
      global_generic_callback != NULL)
    return 1;

  callback_type *callback = NULL;
  callback = (void *)(1 - 1);
  if (callback != NULL) return 2;
  if (!accepts_callback(NULL)) return 3;
  if (returns_callback() != NULL) return 4;

  callback =
      1 ? add_one : NULL;
  if (callback(4) != 5) return 5;

  callback =
      _Generic(0, int: NULL, default: add_one);
  if (callback != NULL) return 6;
  return 0;
}
