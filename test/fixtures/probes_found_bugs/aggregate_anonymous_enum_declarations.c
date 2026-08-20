#include <assert.h>
#include <stddef.h>

struct file_record {
  enum { FILE_WIDTH = 3 };
  int values[FILE_WIDTH];
};

union file_union {
  enum { UNION_WIDTH = 4 };
  int values[UNION_WIDTH];
};

struct flexible_record {
  int count;
  int values[];
  enum { FLEXIBLE_MARKER = 5 };
};

struct promoted_record {
  struct {
    enum { PROMOTED_WIDTH = 2 };
    int values[PROMOTED_WIDTH];
  };
  int tail;
};

_Static_assert(sizeof(struct file_record) == 3 * sizeof(int),
               "enumerator-only declaration adds no member storage");
_Static_assert(sizeof(union file_union) == 4 * sizeof(int),
               "union layout uses only the named array member");
_Static_assert(offsetof(struct flexible_record, values) == sizeof(int),
               "enumerator declaration after flexible array adds no member");

static int local_record_value(void) {
  struct local_record {
    enum { LOCAL_WIDTH = 2 };
    int values[LOCAL_WIDTH];
  };
  struct local_record value = {{11, 13}};
  return value.values[0] + value.values[1];
}

int main(void) {
  struct file_record file = {{2, 3, 5}};
  union file_union item = {{7, 11, 13, 17}};
  struct promoted_record promoted = {
      .values = {19, 23},
      .tail = 29,
  };

  assert(file.values[0] + file.values[1] + file.values[2] == 10);
  assert(item.values[3] == 17);
  assert(promoted.values[0] + promoted.values[1] + promoted.tail == 71);
  assert(local_record_value() == 24);
  assert(FLEXIBLE_MARKER == 5);
  return 0;
}
