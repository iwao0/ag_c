#include <stddef.h>

typedef int prototype_query_callback(
    struct prototype_query_record {
      unsigned char prefix;
      int value;
    } *record,
    unsigned char (*alignment)[_Alignof(struct prototype_query_record)],
    unsigned char (*offset)[offsetof(struct prototype_query_record, value)],
    enum prototype_query_count { PROTOTYPE_QUERY_COUNT = 4 } *count,
    unsigned char (*elements)[PROTOTYPE_QUERY_COUNT],
    unsigned char (*selection)[
        _Generic((struct prototype_query_record *)0,
                 struct prototype_query_record *:
                     sizeof((struct prototype_query_record){0}),
                 default: 1)]);

static prototype_query_callback *callback_slot;

int main(void) {
  return callback_slot != 0;
}
