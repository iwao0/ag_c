#include <stdbool.h>

struct Complete {
  int member;
};

struct Incomplete;

static int identity(int value) {
  return value;
}

int main(void) {
  int value = 7;
  double floating = (double)value;
  unsigned long address = (unsigned long)&value;
  int *integer_roundtrip = (int *)address;
  char *bytes = (char *)&value;
  int *object_roundtrip = (int *)bytes;
  void *generic = (void *)&value;
  int *void_roundtrip = (int *)generic;
  bool present = (bool)&value;
  int (*original)(int) = identity;
  long (*different)(long) = (long (*)(long))original;
  int (*function_roundtrip)(int) =
      (int (*)(int))different;
  int (*object_as_function)(void) =
      (int (*)(void))&value;
  void *function_as_object = (void *)identity;
  struct Complete complete = {11};
  struct Incomplete *incomplete = 0;
  void *incomplete_generic = (void *)incomplete;

  (void)object_as_function;
  (void)function_as_object;
  (void)complete;
  if ((int)floating != value)
    return 1;
  if (integer_roundtrip != &value)
    return 2;
  if (object_roundtrip != &value)
    return 3;
  if (void_roundtrip != &value)
    return 4;
  if (!present)
    return 5;
  if (function_roundtrip(9) != 9)
    return 6;
  if ((struct Incomplete *)incomplete_generic != incomplete)
    return 7;
  return 0;
}
