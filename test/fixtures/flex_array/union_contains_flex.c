#include <assert.h>
#include <stddef.h>

struct payload {
  unsigned int count;
  unsigned char bytes[];
};

union envelope {
  struct payload payload;
  unsigned int raw;
};

static union envelope global_payload = {
    .payload = {.count = 17},
};

static union envelope global_raw = {
    .raw = 29,
};

int main(void) {
  union envelope local_payload = {
      .payload = {.count = 31},
  };
  union envelope local_raw = {
      .raw = 43,
  };

  assert(sizeof(struct payload) == sizeof(unsigned int));
  assert(offsetof(struct payload, bytes) == sizeof(unsigned int));
  assert(sizeof(union envelope) == sizeof(unsigned int));
  assert(_Alignof(union envelope) == _Alignof(unsigned int));
  assert(global_payload.payload.count == 17);
  assert(global_raw.raw == 29);
  assert(local_payload.payload.count == 31);
  assert(local_raw.raw == 43);
  return 0;
}
