#include <assert.h>

static int global_number = 37;

static int plus_one(int value) {
  return value + 1;
}

struct mixed_payload {
  unsigned char tag;
  double scale;
  int *pointer;
  int (*callback)(int);
  unsigned char tail;
};

union mixed_overlay {
  struct mixed_payload value;
  unsigned char raw[64];
};

struct mixed_envelope {
  unsigned char prefix;
  union mixed_overlay payload;
  unsigned char suffix;
};

static union mixed_overlay global_value = {
    .value = {1, 2.5, &global_number, plus_one, 3},
};

static struct mixed_envelope global_envelope = {
    .prefix = 4,
    .payload = {
        .value = {5, 3.5, &global_number, plus_one, 6},
    },
    .suffix = 7,
};

static int overlay_matches(const union mixed_overlay *value,
                           unsigned char tag, double scale,
                           unsigned char tail) {
  return value->value.tag == tag &&
         value->value.scale == scale &&
         value->value.pointer == &global_number &&
         *value->value.pointer == 37 &&
         value->value.callback != 0 &&
         value->value.callback(10) == 11 &&
         value->value.tail == tail;
}

static const union mixed_overlay *static_value(void) {
  static union mixed_overlay value = {
      .value = {8, 4.5, &global_number, plus_one, 9},
  };
  return &value;
}

int main(void) {
  union mixed_overlay local_value = {
      .value = {10, 5.5, &global_number, plus_one, 11},
  };
  assert(overlay_matches(&global_value, 1, 2.5, 3));
  assert(overlay_matches(&local_value, 10, 5.5, 11));
  assert(overlay_matches(static_value(), 8, 4.5, 9));
  assert(global_envelope.prefix == 4);
  assert(overlay_matches(&global_envelope.payload, 5, 3.5, 6));
  assert(global_envelope.suffix == 7);
  return 0;
}
