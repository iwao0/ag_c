#include <assert.h>
#include <stddef.h>

struct padded_value {
  unsigned char head;
  unsigned int word;
  unsigned char tail;
};

union padded_overlay {
  struct padded_value value;
  unsigned char raw[16];
};

struct padded_envelope {
  unsigned char prefix;
  union padded_overlay payload;
  unsigned char suffix;
};

static union padded_overlay global_value = {
    .value = {1, 0, 2},
};

static struct padded_envelope global_envelope = {
    .prefix = 3,
    .payload = {.value = {4, 0x12345678u, 5}},
    .suffix = 6,
};

_Static_assert(sizeof(struct padded_value) == 12,
               "padded active member size");
_Static_assert(sizeof(union padded_overlay) == 16,
               "union largest member size");
_Static_assert(sizeof(struct padded_envelope) == 24,
               "outer struct must retain union and suffix storage");

static int overlay_matches(const union padded_overlay *value,
                           unsigned char head, unsigned int word,
                           unsigned char tail) {
  return value->value.head == head &&
         value->value.word == word &&
         value->value.tail == tail;
}

static const union padded_overlay *static_value(void) {
  static union padded_overlay value = {
      .value = {7, 0x23456789u, 8},
  };
  return &value;
}

int main(void) {
  union padded_overlay local_value = {
      .value = {9, 0x3456789au, 10},
  };
  assert(offsetof(struct padded_value, word) == 4);
  assert(offsetof(struct padded_value, tail) == 8);
  assert(offsetof(struct padded_envelope, payload) == 4);
  assert(offsetof(struct padded_envelope, suffix) == 20);
  assert(overlay_matches(&global_value, 1, 0, 2));
  assert(overlay_matches(&local_value, 9, 0x3456789au, 10));
  assert(overlay_matches(static_value(), 7, 0x23456789u, 8));
  assert(global_envelope.prefix == 3);
  assert(overlay_matches(&global_envelope.payload, 4, 0x12345678u, 5));
  assert(global_envelope.suffix == 6);
  return 0;
}
