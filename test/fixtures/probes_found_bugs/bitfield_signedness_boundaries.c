#include <assert.h>

struct signed_and_unsigned_fields {
  signed int signed_one : 1;
  signed int signed_four : 4;
  unsigned int unsigned_four : 4;
  signed int signed_twelve : 12;
  unsigned int unsigned_twenty : 20;
};

struct nested_fields {
  unsigned char prefix;
  struct signed_and_unsigned_fields fields;
  unsigned char suffix;
};

static struct signed_and_unsigned_fields global_fields = {
    .signed_one = -1,
    .signed_four = -8,
    .unsigned_four = 15,
    .signed_twelve = -2048,
    .unsigned_twenty = 0xfffff,
};

static int values_match(const struct signed_and_unsigned_fields *value) {
  return value->signed_one == -1 &&
         value->signed_four == -8 &&
         value->unsigned_four == 15 &&
         value->signed_twelve == -2048 &&
         value->unsigned_twenty == 0xfffff;
}

static int check_load_signedness(void) {
  assert(values_match(&global_fields));

  struct signed_and_unsigned_fields automatic = {
      .signed_one = -1,
      .signed_four = -7,
      .unsigned_four = 14,
      .signed_twelve = -2047,
      .unsigned_twenty = 0xffffe,
  };
  assert(automatic.signed_one < 0);
  assert(automatic.signed_four == -7);
  assert(automatic.unsigned_four == 14);
  assert(automatic.signed_twelve == -2047);
  assert(automatic.unsigned_twenty == 0xffffe);

  automatic.signed_four += 2;
  automatic.signed_twelve >>= 1;
  automatic.unsigned_twenty >>= 19;
  assert(automatic.signed_four == -5);
  assert(automatic.signed_twelve == -1024);
  assert(automatic.unsigned_twenty == 1);
  return 0;
}

static int check_unsigned_truncation(void) {
  struct signed_and_unsigned_fields value = {0};
  unsigned int four_source = 31;
  unsigned int twenty_source = 0x1fffff;
  value.unsigned_four = four_source;
  value.unsigned_twenty = twenty_source;
  assert(value.unsigned_four == 15);
  assert(value.unsigned_twenty == 0xfffff);

  unsigned int source = 0x12345678u;
  value.unsigned_four = source;
  value.unsigned_twenty = source;
  assert(value.unsigned_four == 8);
  assert(value.unsigned_twenty == 0x45678);
  return 0;
}

static int check_nested_storage(void) {
  struct nested_fields value = {
      .prefix = 11,
      .fields = {
          .signed_one = -1,
          .signed_four = -8,
          .unsigned_four = 15,
          .signed_twelve = -2048,
          .unsigned_twenty = 0xfffff,
      },
      .suffix = 22,
  };
  assert(value.prefix == 11);
  assert(values_match(&value.fields));
  assert(value.suffix == 22);
  return 0;
}

int main(void) {
  assert(check_load_signedness() == 0);
  assert(check_unsigned_truncation() == 0);
  assert(check_nested_storage() == 0);
  return 0;
}
