/*
 * Preserve encoded character-array initialization when the array is the
 * selected member of a union.  Keep every assertion on the active member.
 */
#include <assert.h>
#include <uchar.h>
#include <wchar.h>

union encoded_value {
  unsigned long long raw;
  char utf8[6];
  char16_t utf16[4];
  char32_t utf32[3];
  wchar_t wide[3];
};

union exact_encoded_value {
  char utf8[4];
  char16_t utf16[2];
  char32_t utf32[1];
  wchar_t wide[1];
};

union nested_encoded_value {
  unsigned long long raw;
  union encoded_value value;
};

struct encoded_envelope {
  unsigned char prefix;
  union encoded_value value;
  unsigned char suffix;
};

struct anonymous_encoded_envelope {
  unsigned char prefix;
  union {
    unsigned long long raw;
    char16_t utf16[4];
    char32_t utf32[3];
  };
  unsigned char suffix;
};

static union encoded_value global_utf16 = {
    .utf16 = u"\U0001F600",
};
static union exact_encoded_value global_exact_utf32 = {
    .utf32 = U"\U0001F600",
};
static union nested_encoded_value global_nested = {
    .value.wide = L"\U0001F600",
};
static struct encoded_envelope global_envelope = {
    .prefix = 7,
    .value.utf8 = u8"\U0001F600",
    .suffix = 9,
};
static struct anonymous_encoded_envelope global_anonymous = {
    .prefix = 21,
    .utf16 = u"\U0001F600",
    .suffix = 23,
};
static union encoded_value global_values[4] = {
    [0].utf8 = u8"\U0001F600",
    [1].utf16 = u"\U0001F600",
    [2].utf32 = U"\U0001F600",
    [3].wide = L"\U0001F600",
};

static int check_global_objects(void) {
  assert(global_utf16.utf16[0] == 0xD83D);
  assert(global_utf16.utf16[1] == 0xDE00);
  assert(global_utf16.utf16[2] == 0);
  assert(global_utf16.utf16[3] == 0);
  assert(global_exact_utf32.utf32[0] == 0x1F600);
  assert(global_nested.value.wide[0] == 0x1F600);
  assert(global_nested.value.wide[1] == 0);
  assert(global_envelope.prefix == 7);
  assert((unsigned char)global_envelope.value.utf8[0] == 0xF0);
  assert((unsigned char)global_envelope.value.utf8[3] == 0x80);
  assert(global_envelope.value.utf8[4] == 0);
  assert(global_envelope.value.utf8[5] == 0);
  assert(global_envelope.suffix == 9);
  assert(global_anonymous.prefix == 21);
  assert(global_anonymous.utf16[0] == 0xD83D);
  assert(global_anonymous.utf16[1] == 0xDE00);
  assert(global_anonymous.utf16[2] == 0);
  assert(global_anonymous.suffix == 23);
  assert((unsigned char)global_values[0].utf8[0] == 0xF0);
  assert(global_values[0].utf8[4] == 0);
  assert(global_values[1].utf16[0] == 0xD83D);
  assert(global_values[1].utf16[1] == 0xDE00);
  assert(global_values[1].utf16[2] == 0);
  assert(global_values[2].utf32[0] == 0x1F600);
  assert(global_values[2].utf32[1] == 0);
  assert(global_values[3].wide[0] == 0x1F600);
  assert(global_values[3].wide[1] == 0);
  return 0;
}

static int check_static_local_objects(void) {
  static union encoded_value utf32 = {
      .utf32 = U"\U0001F600",
  };
  static struct encoded_envelope envelope = {
      .prefix = 11,
      .value.utf16 = u"\U0001F600",
      .suffix = 13,
  };

  assert(utf32.utf32[0] == 0x1F600);
  assert(utf32.utf32[1] == 0);
  assert(utf32.utf32[2] == 0);
  assert(envelope.prefix == 11);
  assert(envelope.value.utf16[0] == 0xD83D);
  assert(envelope.value.utf16[1] == 0xDE00);
  assert(envelope.value.utf16[2] == 0);
  assert(envelope.suffix == 13);
  return 0;
}

static int check_automatic_objects(void) {
  union encoded_value utf8 = {
      .utf8 = u8"\U0001F600",
  };
  union exact_encoded_value exact_utf16 = {
      .utf16 = u"\U0001F600",
  };
  union nested_encoded_value nested = {
      .value.utf32 = U"\U0001F600",
  };
  struct encoded_envelope envelope = {
      .prefix = 17,
      .value.wide = L"\U0001F600",
      .suffix = 19,
  };
  struct anonymous_encoded_envelope anonymous = {
      .prefix = 25,
      .utf32 = U"\U0001F600",
      .suffix = 27,
  };

  assert((unsigned char)utf8.utf8[0] == 0xF0);
  assert((unsigned char)utf8.utf8[3] == 0x80);
  assert(utf8.utf8[4] == 0);
  assert(utf8.utf8[5] == 0);
  assert(exact_utf16.utf16[0] == 0xD83D);
  assert(exact_utf16.utf16[1] == 0xDE00);
  assert(nested.value.utf32[0] == 0x1F600);
  assert(nested.value.utf32[1] == 0);
  assert(envelope.prefix == 17);
  assert(envelope.value.wide[0] == 0x1F600);
  assert(envelope.value.wide[1] == 0);
  assert(envelope.suffix == 19);
  assert(anonymous.prefix == 25);
  assert(anonymous.utf32[0] == 0x1F600);
  assert(anonymous.utf32[1] == 0);
  assert(anonymous.suffix == 27);
  return 0;
}

static int check_compound_literals(void) {
  union encoded_value *utf16 =
      &(union encoded_value){.utf16 = u"\U0001F600"};
  union exact_encoded_value *exact_wide =
      &(union exact_encoded_value){.wide = L"\U0001F600"};
  union nested_encoded_value *nested =
      &(union nested_encoded_value){
          .value.utf8 = u8"\U0001F600",
      };
  union encoded_value *values =
      (union encoded_value[]){
          [1].utf32 = U"\U0001F600",
      };
  struct anonymous_encoded_envelope *anonymous =
      &(struct anonymous_encoded_envelope){
          .prefix = 29,
          .utf16 = u"\U0001F600",
          .suffix = 31,
      };

  assert(utf16->utf16[0] == 0xD83D);
  assert(utf16->utf16[1] == 0xDE00);
  assert(utf16->utf16[2] == 0);
  assert(exact_wide->wide[0] == 0x1F600);
  assert((unsigned char)nested->value.utf8[0] == 0xF0);
  assert(nested->value.utf8[4] == 0);
  assert(values[0].raw == 0);
  assert(values[1].utf32[0] == 0x1F600);
  assert(values[1].utf32[1] == 0);
  assert(anonymous->prefix == 29);
  assert(anonymous->utf16[0] == 0xD83D);
  assert(anonymous->utf16[1] == 0xDE00);
  assert(anonymous->suffix == 31);
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
