#include <assert.h>

/*
 * The containing and nested anonymous unions begin at the same byte offset.
 * The bit-field allocation unit also overlaps the final two bytes of text.
 * Switching from raw storage to the promoted members must preserve both the
 * selected inner member and the initialized character bytes.
 */
struct nested_text_value {
  union {
    struct {
      union {
        long long raw;
        struct {
          char text[6];
          unsigned int kind : 3;
          unsigned int code : 5;
        };
      };
    };
    unsigned char outer_raw[16];
  };
  int tail;
};

static struct nested_text_value global_value = {
    .outer_raw = {[0] = 0xff, [15] = 0xff},
    .text = "abcde",
    .kind = 5,
    .code = 17,
    .tail = 21,
};

static struct nested_text_value global_values[] = {
    [2].outer_raw[8] = 0xff,
    [2].text = "xy",
    [2].kind = 3,
    [2].code = 19,
    [2].tail = 22,
};

static int value_matches(
    const struct nested_text_value *value, const char *text,
    unsigned int kind, unsigned int code, int tail) {
  int index = 0;
  while (text[index] != '\0') {
    if (value->text[index] != text[index]) return 0;
    index++;
  }
  return value->text[index] == '\0' &&
         value->kind == kind &&
         value->code == code &&
         value->tail == tail;
}

static int check_global_objects(void) {
  assert(value_matches(&global_value, "abcde", 5, 17, 21));
  assert(global_value.text[4] == 'e');
  assert(global_value.text[5] == '\0');

  assert(sizeof(global_values) / sizeof(global_values[0]) == 3);
  assert(global_values[0].raw == 0);
  assert(global_values[0].tail == 0);
  assert(global_values[1].raw == 0);
  assert(global_values[1].tail == 0);
  assert(value_matches(&global_values[2], "xy", 3, 19, 22));
  assert(global_values[2].text[3] == '\0');
  assert(global_values[2].text[4] == '\0');
  assert(global_values[2].text[5] == '\0');
  return 0;
}

static int check_static_local_objects(void) {
  static struct nested_text_value value = {
      .outer_raw = {[4] = 0xff},
      .text = "stat",
      .kind = 6,
      .code = 23,
      .tail = 24,
  };
  static struct nested_text_value values[] = {
      [1] = {
          .raw = -1,
          .text = "s",
          .kind = 2,
          .code = 25,
          .tail = 26,
      },
  };

  assert(value_matches(&value, "stat", 6, 23, 24));
  assert(value.text[5] == '\0');
  assert(sizeof(values) / sizeof(values[0]) == 2);
  assert(values[0].raw == 0);
  assert(values[0].tail == 0);
  assert(value_matches(&values[1], "s", 2, 25, 26));
  return 0;
}

static int check_automatic_objects(void) {
  struct nested_text_value value = {
      .outer_raw = {[7] = 0xff},
      .text = "local",
      .kind = 1,
      .code = 27,
      .tail = 28,
  };
  struct nested_text_value values[3] = {
      [2] = {
          .raw = -1,
          .text = "L",
          .kind = 4,
          .code = 29,
          .tail = 30,
      },
  };

  assert(value_matches(&value, "local", 1, 27, 28));
  assert(sizeof(values) / sizeof(values[0]) == 3);
  assert(values[0].raw == 0);
  assert(values[1].raw == 0);
  assert(value_matches(&values[2], "L", 4, 29, 30));
  return 0;
}

static int check_compound_literals(void) {
  struct nested_text_value *value =
      &(struct nested_text_value){
          .outer_raw = {[12] = 0xff},
          .text = "lit",
          .kind = 7,
          .code = 31,
          .tail = 32,
      };
  struct nested_text_value *values =
      (struct nested_text_value[]){
          [1] = {
              .raw = -1,
              .text = "C",
              .kind = 5,
              .code = 7,
              .tail = 33,
          },
      };

  assert(value_matches(value, "lit", 7, 31, 32));
  assert(values[0].raw == 0);
  assert(values[0].tail == 0);
  assert(value_matches(&values[1], "C", 5, 7, 33));
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
