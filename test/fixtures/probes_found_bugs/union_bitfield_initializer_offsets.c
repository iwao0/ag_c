#include <assert.h>

struct bit_payload {
  unsigned int low : 4;
  unsigned int high : 4;
  unsigned char tail;
};

union bit_overlay {
  struct bit_payload bits;
  unsigned char raw[16];
};

struct bit_envelope {
  unsigned char prefix;
  union bit_overlay payload;
  unsigned char suffix;
};

static union bit_overlay global_value = {
    .bits = {.low = 3, .high = 10, .tail = 11},
};

static struct bit_envelope global_envelope = {
    .prefix = 12,
    .payload = {.bits = {.low = 4, .high = 9, .tail = 13}},
    .suffix = 14,
};

static int overlay_matches(const union bit_overlay *value,
                           unsigned int low, unsigned int high,
                           unsigned char tail) {
  return value->bits.low == low &&
         value->bits.high == high &&
         value->bits.tail == tail;
}

static const union bit_overlay *static_value(void) {
  static union bit_overlay value = {
      .bits = {.low = 5, .high = 8, .tail = 15},
  };
  return &value;
}

int main(void) {
  union bit_overlay local_value = {
      .bits = {.low = 6, .high = 7, .tail = 16},
  };
  assert(overlay_matches(&global_value, 3, 10, 11));
  assert(overlay_matches(&local_value, 6, 7, 16));
  assert(overlay_matches(static_value(), 5, 8, 15));
  assert(global_envelope.prefix == 12);
  assert(overlay_matches(&global_envelope.payload, 4, 9, 13));
  assert(global_envelope.suffix == 14);
  return 0;
}
