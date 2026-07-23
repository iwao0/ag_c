#include <assert.h>

struct record;

static int read_record(struct record *restrict pointer);

struct record {
  int value;
};

typedef struct record *record_pointer;

static int read_record(struct record *restrict pointer) {
  return pointer->value;
}

int main(void) {
  struct record value = {40};
  void *restrict erased = &value;
  restrict record_pointer restored = erased;

  assert(read_record(restored) == 40);
  restored->value += 2;
  assert(read_record(&value) == 42);
  return 0;
}
