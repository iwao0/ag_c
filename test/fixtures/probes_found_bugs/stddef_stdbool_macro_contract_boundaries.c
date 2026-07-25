#include <stdbool.h>
#include <stddef.h>

struct leaf {
  char prefix;
  int value;
};

struct container {
  char lead;
  struct leaf items[3];
  char flexible[];
};

union overlay {
  int word;
  char bytes[4];
};

enum {
  nested_value_offset = offsetof(struct container, items[2].value),
  flexible_offset = offsetof(struct container, flexible),
  overlay_bytes_offset = offsetof(union overlay, bytes)
};

static int offset_is_constant[nested_value_offset + 1];

_Static_assert(_Generic(true, int: 1, default: 0),
               "true must be an int constant");
_Static_assert(_Generic(false, int: 1, default: 0),
               "false must be an int constant");
_Static_assert(_Generic((bool)true, _Bool: 1, default: 0),
               "bool must name _Bool");
_Static_assert(_Generic(NULL, void *: 1, default: 0),
               "this target defines NULL as void pointer");
_Static_assert(_Generic(offsetof(struct container, items), size_t: 1,
                        default: 0),
               "offsetof must have type size_t");
_Static_assert(nested_value_offset ==
                   offsetof(struct container, items) +
                       2 * sizeof(struct leaf) +
                       offsetof(struct leaf, value),
               "nested member designator offset");
_Static_assert(flexible_offset ==
                   offsetof(struct container, items) +
                       3 * sizeof(struct leaf),
               "flexible array member offset");
_Static_assert(overlay_bytes_offset == 0, "union member offset");

int main(void) {
  bool truth = true;
  bool lie = false;
  int *pointer = NULL;

  if (!truth || lie || pointer)
    return 1;
  if (__bool_true_false_are_defined != 1)
    return 2;
  if (offset_is_constant[0] != 0)
    return 3;
  return 0;
}
